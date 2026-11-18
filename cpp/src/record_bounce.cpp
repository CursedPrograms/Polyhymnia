// Polyhymnia - Audio recording, input monitoring, punch in/out, loudness metering, and offline bounce/export
// (after SoundCraft crates/playback/src/record.rs, crates/engine/src/io/record.rs, crates/engine/src/io.rs,
// crates/dsp/src/meter.rs, crates/ui-egui/src/bounce.rs, crates/ui-egui/src/dialogs.rs).
#include "record_bounce.h"
#include "engine_edit.h"
#include "mix.h"
#include "playback.h"
#include "undo.h"

#include "miniaudio.h"
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace fs = std::filesystem;

namespace RecordBounce {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxBounceSecs = 4.0 * 3600.0;

// after SoundCraft (crates/playback/src/record.rs finite)
float finiteSample(float x) {
    return std::isfinite(x) ? x : 0.0f;
}

uint32_t floatToBits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(uint32_t));
    return u;
}

float bitsToFloat(uint32_t u) {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(float));
    return f;
}

size_t nextPow2(size_t n) {
    size_t p = 2;
    while (p < n && p < (size_t(1) << 30)) p <<= 1;
    return p;
}

// after SoundCraft (crates/engine/src/io/render.rs sanitize_name)
std::string sanitizeName(const std::string& n) {
    std::string s;
    s.reserve(n.size());
    for (char c : n) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == ' ' || c == '.') {
            s.push_back(c);
        } else {
            s.push_back('_');
        }
    }
    size_t a = 0, b = s.size();
    while (a < b && (std::isspace(static_cast<unsigned char>(s[a])) || s[a] == '.')) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    std::string trimmed = s.substr(a, b - a);
    if (trimmed.empty()) return "untitled";
    if (trimmed.size() > 100) trimmed.resize(100);
    return trimmed;
}

std::string toLowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void writeU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

} // namespace

// ─── CaptureRing & SharedCapture (after crates/playback/src/record.rs) ──────────────────────────

CaptureRing::CaptureRing(size_t capacity)
    : slots_(nextPow2(std::max<size_t>(2, capacity))), mask_(slots_.size() - 1) {
    for (auto& s : slots_) s.store(0, std::memory_order_relaxed);
}

size_t CaptureRing::push(size_t n, const std::function<float(size_t)>& sample) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);
    const size_t used = head - tail;
    const size_t room = (slots_.size() > used) ? (slots_.size() - used) : 0;
    const size_t count = std::min(n, room);
    for (size_t i = 0; i < count; ++i) {
        slots_[(head + i) & mask_].store(floatToBits(sample(i)), std::memory_order_relaxed);
    }
    head_.store(head + count, std::memory_order_release);
    return count;
}

void CaptureRing::popAll(std::vector<float>& out) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t count = head - tail;
    out.reserve(out.size() + count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(bitsToFloat(slots_[(tail + i) & mask_].load(std::memory_order_relaxed)));
    }
    tail_.store(head, std::memory_order_release);
}

void CaptureRing::clear() {
    tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
}

SharedCapture::SharedCapture(size_t ringCapacity)
    : ring(ringCapacity) {}

// after SoundCraft (crates/playback/src/record.rs Shared::guarded)
void SharedCapture::guarded(const std::function<void()>& work) {
    beats.fetch_add(1, std::memory_order_relaxed);
    if (fault.load(std::memory_order_relaxed) != kFaultBroken) {
        try {
            work();
        } catch (...) {
            fault.store(kFaultBroken, std::memory_order_relaxed);
        }
    }
}

// after SoundCraft (crates/playback/src/record.rs Shared::capture)
void SharedCapture::capture(const float* data, size_t len, size_t& owed) {
    owed -= ring.push(owed, [](size_t) { return 0.0f; });
    const size_t kept = (owed == 0)
        ? ring.push(len, [&](size_t i) { return (data && i < len) ? finiteSample(data[i]) : 0.0f; })
        : 0;
    const size_t lost = len - kept;
    owed += lost;
    dropped.fetch_add(lost, std::memory_order_relaxed);
}

// ─── InputRing (after crates/playback/src/record.rs InputRing) ──────────────────────────────────

InputRing::InputRing(size_t ch, uint32_t rate)
    : channels(std::max<size_t>(1, ch)),
      rate_(rate),
      cap_((rate == 0 ? 24000 : static_cast<size_t>(rate) / 2) * std::max<size_t>(1, ch)) {}

void InputRing::push(const float* samples, size_t len) {
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock() || !samples) return;
    for (size_t i = 0; i < len; ++i) {
        state_.data.push_back(finiteSample(samples[i]));
    }
    while (state_.data.size() > cap_) {
        state_.data.pop_front();
    }
}

// after SoundCraft (crates/playback/src/record.rs InputRing::pop_into)
void InputRing::popInto(std::vector<std::vector<float>>& out, size_t frames, uint32_t sessionRate) {
    const size_t ch = std::max<size_t>(1, channels.load(std::memory_order_relaxed));
    for (auto& c : out) {
        const size_t n = std::min(frames, c.size());
        for (size_t i = 0; i < n; ++i) c[i] = 0.0f;
    }
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock()) return;

    const double step = (rate_ == 0 || sessionRate == 0) ? 1.0 : static_cast<double>(rate_) / static_cast<double>(sessionRate);
    for (size_t f = 0; f < frames; ++f) {
        while (state_.phase >= 1.0 && state_.data.size() >= 2 * ch) {
            state_.data.erase(state_.data.begin(), state_.data.begin() + static_cast<std::ptrdiff_t>(ch));
            state_.phase -= 1.0;
        }
        if (state_.phase >= 1.0 || state_.data.size() < 2 * ch) {
            break;
        }
        const float fr = static_cast<float>(state_.phase);
        for (size_t c = 0; c < ch; ++c) {
            const float a = (c < state_.data.size()) ? state_.data[c] : 0.0f;
            const float b = (ch + c < state_.data.size()) ? state_.data[ch + c] : a;
            if (c < out.size() && f < out[c].size()) {
                out[c][f] = a + (b - a) * fr;
            }
        }
        state_.phase += step;
    }
}

// ─── TakeWriter & Recorder (after crates/playback/src/record.rs) ────────────────────────────────

std::vector<uint8_t> wavFloatHeader(uint32_t sampleRate, uint16_t channels, uint64_t frames) {
    const uint16_t nCh = std::max<uint16_t>(1, channels);
    const uint32_t sr = sampleRate > 0 ? sampleRate : 48000;
    const uint16_t blockAlign = nCh * 4;
    const uint32_t byteRate = sr * blockAlign;
    const uint64_t dataBytes64 = frames * blockAlign;
    const uint32_t dataBytes = static_cast<uint32_t>(std::min<uint64_t>(dataBytes64, 0xFFFFFF00ULL));
    const uint32_t riffSize = 36u + dataBytes;

    std::vector<uint8_t> h;
    h.reserve(44);
    h.insert(h.end(), {'R', 'I', 'F', 'F'});
    writeU32LE(h, riffSize);
    h.insert(h.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    writeU32LE(h, 16u);
    writeU16LE(h, 3u); // IEEE float
    writeU16LE(h, nCh);
    writeU32LE(h, sr);
    writeU32LE(h, byteRate);
    writeU16LE(h, blockAlign);
    writeU16LE(h, 32u);
    h.insert(h.end(), {'d', 'a', 't', 'a'});
    writeU32LE(h, dataBytes);
    return h;
}

std::pair<std::unique_ptr<TakeWriter>, std::string>
TakeWriter::create(uint64_t id, std::vector<size_t> inputs, fs::path path, uint32_t rate) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) return {nullptr, path.u8string() + ": " + ec.message()};
    }
    if (fs::exists(path, ec)) {
        return {nullptr, path.u8string() + ": file already exists"};
    }
    auto tw = std::make_unique<TakeWriter>();
    tw->trackId = id;
    tw->inputs = std::move(inputs);
    tw->path = std::move(path);
    tw->rate = rate;
    tw->liveMtx = std::make_shared<std::mutex>();
    tw->live = std::make_shared<LiveTake>(tw->inputs.size());
    tw->file.open(tw->path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!tw->file.is_open()) {
        return std::make_pair(std::unique_ptr<TakeWriter>{}, tw->path.u8string() + ": cannot create take file");
    }
    tw->writeHeader();
    if (tw->error.has_value()) {
        return std::make_pair(std::unique_ptr<TakeWriter>{}, *tw->error);
    }
    return std::make_pair(std::move(tw), std::string{});
}

void TakeWriter::write(const float* block, size_t blockSamples, size_t channels) {
    const size_t ch = std::max<size_t>(1, channels);
    if (error.has_value() || !block || blockSamples < ch) return;
    const size_t nFrames = blockSamples / ch;
    std::vector<uint8_t> bytes;
    bytes.reserve(nFrames * inputs.size() * 4);
    for (size_t f = 0; f < nFrames; ++f) {
        const float* frame = block + f * ch;
        for (size_t inIdx : inputs) {
            const float v = (inIdx < ch) ? frame[inIdx] : 0.0f;
            writeU32LE(bytes, floatToBits(v));
        }
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file.good()) {
        fail("disk write failed");
        return;
    }
    summarize(block, nFrames * ch, ch);
    frames += nFrames;
}

void TakeWriter::summarize(const float* block, size_t blockSamples, size_t channels) {
    const size_t ch = std::max<size_t>(1, channels);
    const size_t nFrames = blockSamples / ch;
    std::lock_guard<std::mutex> lk(*liveMtx);
    for (size_t f = 0; f < nFrames; ++f) {
        const uint64_t n = frames + f;
        const size_t at = static_cast<size_t>(n / kLiveBlock);
        const float* frame = block + f * ch;
        for (size_t c = 0; c < inputs.size() && c < live->size(); ++c) {
            const size_t inIdx = inputs[c];
            const float v = (inIdx < ch) ? frame[inIdx] : 0.0f;
            auto& blocks = (*live)[c];
            if (at < blocks.size()) {
                blocks[at].first = std::min(blocks[at].first, v);
                blocks[at].second = std::max(blocks[at].second, v);
            } else if (at < kLiveBlocks) {
                blocks.emplace_back(v, v);
            }
        }
    }
}

void TakeWriter::writeHeader() {
    const auto header = wavFloatHeader(rate, static_cast<uint16_t>(std::max<size_t>(1, inputs.size())), frames);
    file.seekp(0, std::ios::beg);
    if (!file.good()) {
        fail("seek failed");
        return;
    }
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    file.seekp(0, std::ios::end);
    file.flush();
    if (!file.good()) {
        fail("write header failed");
    }
}

void TakeWriter::fail(const std::string& msg) {
    if (!error.has_value()) {
        error = path.u8string() + ": " + msg;
    }
}

// after SoundCraft (crates/playback/src/record.rs write_takes)
Takes writeTakes(SharedCapture& shared, std::vector<std::unique_ptr<TakeWriter>> takes, size_t channels) {
    const size_t ch = std::max<size_t>(1, channels);
    std::vector<float> block;
    auto headerWritten = std::chrono::steady_clock::now();
    bool reported = false;

    for (;;) {
        const bool stopping = !shared.armed.load(std::memory_order_acquire);
        shared.ring.popAll(block);
        const size_t whole = block.size() - (block.size() % ch);
        if (whole > 0) {
            for (auto& t : takes) {
                t->write(block.data(), whole, ch);
            }
            block.erase(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(whole));
        }
        if (!reported) {
            for (const auto& t : takes) {
                if (t->error.has_value()) {
                    std::lock_guard<std::mutex> lk(shared.failedMtx);
                    shared.failed = t->error;
                    reported = true;
                    break;
                }
            }
        }
        if (stopping) break;
        const auto now = std::chrono::steady_clock::now();
        if (now - headerWritten >= std::chrono::seconds(1)) {
            headerWritten = now;
            for (auto& t : takes) t->writeHeader();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::vector<std::pair<uint64_t, fs::path>> done;
    std::optional<std::string> problem;
    for (auto& t : takes) {
        t->writeHeader();
        t->file.close();
        if (t->frames == 0) {
            std::error_code ec;
            fs::remove(t->path, ec);
        } else {
            done.emplace_back(t->trackId, t->path);
        }
        if (!problem.has_value() && t->error.has_value()) {
            problem = t->error;
        }
    }
    return {std::move(done), std::move(problem)};
}

struct Recorder::DeviceImpl {
    ma_device dev{};
    bool active = false;
    size_t owed = 0;
    std::shared_ptr<SharedCapture> shared;
    std::shared_ptr<InputRing> monitor;

    ~DeviceImpl() {
        if (active) {
            ma_device_uninit(&dev);
            active = false;
        }
    }
};

Recorder::Recorder(std::shared_ptr<SharedCapture> sh, std::shared_ptr<InputRing> mon,
                   std::string name, uint32_t sr, size_t ch)
    : shared(std::move(sh)),
      monitor(std::move(mon)),
      deviceName(std::move(name)),
      sampleRate(sr),
      channels(std::max<size_t>(1, ch)),
      seen(0, std::chrono::steady_clock::now()) {}

Recorder::~Recorder() {
    finish();
}

std::pair<std::unique_ptr<Recorder>, std::string> Recorder::open(uint32_t preferredRate, size_t preferredChannels) {
    const uint32_t sr = preferredRate > 0 ? preferredRate : 48000;
    const size_t ch = std::max<size_t>(1, preferredChannels);
    const size_t ringCap = std::min<size_t>(size_t(sr) * ch * kRingSeconds, size_t(1) << 25);
    auto shared = std::make_shared<SharedCapture>(ringCap);
    auto monitor = std::make_shared<InputRing>(ch, sr);

    auto rec = std::make_unique<Recorder>(shared, monitor, "Default Input", sr, ch);
    auto impl = std::make_unique<DeviceImpl>();
    impl->shared = shared;
    impl->monitor = monitor;

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.sampleRate = sr;
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = static_cast<ma_uint32>(ch);
    cfg.pUserData = impl.get();
    cfg.dataCallback = [](ma_device* pDev, void*, const void* pInput, ma_uint32 frameCount) {
        auto* d = static_cast<DeviceImpl*>(pDev->pUserData);
        if (!d || !d->shared || !pInput || frameCount == 0) return;
        const size_t nCh = std::max<size_t>(1, pDev->capture.channels);
        const size_t nSamples = static_cast<size_t>(frameCount) * nCh;
        const float* inF32 = static_cast<const float*>(pInput);
        d->shared->guarded([&]() {
            if (d->monitor) d->monitor->push(inF32, nSamples);
            if (d->shared->armed.load(std::memory_order_acquire)) {
                d->shared->capture(inF32, nSamples, d->owed);
            } else {
                d->owed = 0;
            }
        });
    };

    if (ma_device_init(nullptr, &cfg, &impl->dev) == MA_SUCCESS) {
        if (ma_device_start(&impl->dev) == MA_SUCCESS) {
            impl->active = true;
            rec->deviceName = impl->dev.capture.name[0] ? impl->dev.capture.name : "Default Input";
            rec->sampleRate = impl->dev.sampleRate > 0 ? impl->dev.sampleRate : sr;
            rec->channels = std::max<size_t>(1, impl->dev.capture.channels);
            rec->dev_ = std::move(impl);
        } else {
            ma_device_uninit(&impl->dev);
        }
    }
    return std::make_pair(std::move(rec), std::string{});
}

std::string Recorder::arm(const std::vector<std::tuple<uint64_t, std::vector<size_t>, fs::path>>& takes) {
    if (writerThread_.has_value()) {
        return "already recording";
    }
    std::vector<std::unique_ptr<TakeWriter>> writers;
    liveTakes_.clear();
    for (const auto& [id, inputs, path] : takes) {
        auto [tw, err] = TakeWriter::create(id, inputs, path, sampleRate);
        if (!tw) return err;
        liveTakes_.emplace_back(id, tw->liveMtx, tw->live);
        writers.push_back(std::move(tw));
    }
    shared->ring.clear();
    {
        std::lock_guard<std::mutex> lk(shared->failedMtx);
        shared->failed = std::nullopt;
    }
    shared->armed.store(true, std::memory_order_release);
    auto sh = shared;
    const size_t ch = channels;
    writerThread_.emplace([this, sh, w = std::move(writers), ch]() mutable {
        Takes res = writeTakes(*sh, std::move(w), ch);
        std::lock_guard<std::mutex> lk(resultMtx_);
        writerResult_ = std::move(res);
    });
    return {};
}

std::optional<LiveTake> Recorder::liveTake(uint64_t trackId) const {
    for (const auto& [id, mtx, lt] : liveTakes_) {
        if (id == trackId && mtx && lt) {
            std::lock_guard<std::mutex> lk(*mtx);
            return *lt;
        }
    }
    return std::nullopt;
}

// after SoundCraft (crates/playback/src/record.rs Recorder::trouble)
std::optional<Trouble> Recorder::trouble() {
    {
        std::lock_guard<std::mutex> lk(shared->failedMtx);
        if (shared->failed.has_value()) {
            std::string e = std::move(*shared->failed);
            shared->failed = std::nullopt;
            return Trouble::disk(std::move(e));
        }
    }
    switch (shared->fault.load(std::memory_order_relaxed)) {
    case SharedCapture::kFaultGone:
        return Trouble::device("\xe2\x80\x9c" + deviceName + "\xe2\x80\x9d is unavailable");
    case SharedCapture::kFaultBroken:
        return Trouble::device("the audio input hit an internal error (see the log)");
    default:
        break;
    }
    const uint64_t b = shared->beats.load(std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    if (b != seen.first || !shared->armed.load(std::memory_order_relaxed)) {
        seen = {b, now};
    }
    if (now - seen.second >= std::chrono::seconds(2)) {
        return Trouble::device("\xe2\x80\x9c" + deviceName + "\xe2\x80\x9d stopped sending audio");
    }
    return std::nullopt;
}

uint64_t Recorder::lostMs() const {
    const size_t frames = shared->dropped.load(std::memory_order_relaxed) / std::max<size_t>(1, channels);
    return (static_cast<uint64_t>(frames) * 1000ULL) / std::max<uint64_t>(1, sampleRate);
}

Takes Recorder::finish() {
    shared->armed.store(false, std::memory_order_release);
    if (writerThread_.has_value()) {
        if (writerThread_->joinable()) writerThread_->join();
        writerThread_.reset();
    }
    liveTakes_.clear();
    Takes res;
    {
        std::lock_guard<std::mutex> lk(resultMtx_);
        res = std::move(writerResult_);
        writerResult_ = {};
    }
    const size_t lost = shared->dropped.exchange(0, std::memory_order_relaxed) / std::max<size_t>(1, channels);
    if (!res.second.has_value() && lost > 0) {
        const size_t ms = (lost * 1000) / std::max<size_t>(1, sampleRate);
        res.second = std::to_string(ms) + " ms of input lost: the disk was too slow";
    }
    return res;
}

// ─── Engine Recording Plan & Placement (after crates/engine/src/io/record.rs, io.rs) ────────────

// after SoundCraft (crates/engine/src/io.rs plan_takes)
std::vector<std::tuple<uint64_t, std::vector<size_t>, fs::path>>
planTakes(const Session::Document& doc, const fs::path& unsavedDir, size_t inputChannels) {
    const fs::path dir = !doc.folder.empty() ? fs::u8path(doc.folder) : unsavedDir;
    const size_t takeNo = doc.sources.size() + 1;
    const size_t inCh = std::max<size_t>(1, inputChannels);
    std::vector<std::tuple<uint64_t, std::vector<size_t>, fs::path>> out;

    for (const auto& t : doc.tracks) {
        if (!t.recordArm || t.kind != Session::TrackKind::Audio || t.inactive) continue;
        size_t first = 0;
        if (t.mixer.input.kind == Session::Route::Kind::Hardware) {
            std::string h = t.mixer.input.hwName;
            if (h.rfind("In ", 0) == 0) h = h.substr(3);
            const size_t sep = h.find_first_of("- ");
            const std::string token = (sep == std::string::npos) ? h : h.substr(0, sep);
            try {
                const size_t parsed = static_cast<size_t>(std::stoul(token));
                first = parsed > 0 ? (parsed - 1) : 0;
            } catch (...) {
                first = 0;
            }
        }
        const size_t trackCh = Session::channelFormatChannels(t.format);
        std::vector<size_t> inputs;
        inputs.reserve(trackCh);
        for (size_t k = 0; k < trackCh; ++k) {
            inputs.push_back((first + k) % inCh);
        }
        const std::string clean = sanitizeName(t.name);
        auto makeFile = [&](size_t n) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "_%02zu.wav", n);
            return dir / "Audio Files" / (clean + buf);
        };
        fs::path chosen = makeFile(takeNo);
        for (size_t n = takeNo; n < takeNo + 1000; ++n) {
            fs::path cand = makeFile(n);
            std::error_code ec;
            if (!fs::exists(cand, ec)) {
                chosen = std::move(cand);
                break;
            }
        }
        out.emplace_back(t.numId, std::move(inputs), std::move(chosen));
    }
    return out;
}

// after SoundCraft (crates/engine/src/io/record.rs live_span)
std::optional<std::pair<long long, Time::Range>>
liveSpan(long long start, std::optional<long long> pass, std::optional<Time::Range> punch, long long len) {
    const long long passSub = (pass.has_value() && *pass > 0) ? ((len / *pass) * (*pass)) : 0;
    const long long origin = start - passSub;
    const Time::Range span = {start, origin + len};
    const Time::Range target = punch.value_or(span);
    auto inter = span.intersectOpt(target);
    if (!inter.has_value()) return std::nullopt;
    return std::make_pair(origin, *inter);
}

namespace {

std::vector<uint64_t> addRecordingIn(Session::Document& doc, long long start,
                                     const std::vector<std::pair<uint64_t, fs::path>>& takes,
                                     std::optional<long long> pass,
                                     std::optional<Time::Range> punch) {
    const Session::Document before = doc;
    std::vector<uint64_t> out;

    for (const auto& [tid, path] : takes) {
        std::ifstream f(path, std::ios::binary);
        if (!f) continue;
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        ImportAudio::AudioInfo info;
        Session::AudioBuffer buf;
        std::string err;
        if (!ImportAudio::decodeBytes(bytes.data(), bytes.size(), path.extension().u8string(), info, buf, err)) {
            continue;
        }
        Session::Track* tr = Session::findTrackByNum(doc, tid);
        if (!tr) continue;

        const uint32_t hz = static_cast<uint32_t>(doc.sampleRate > 0 ? doc.sampleRate : 48000.0);
        const bool resampled = (buf.sampleRate != hz);
        if (resampled && buf.sampleRate > 0) {
            auto [resBuf, rateErr] = ImportAudio::atRate(std::move(buf), hz);
            if (!resBuf.has_value()) continue;
            buf = std::move(*resBuf);
        }

        const std::string name = path.stem().u8string().empty() ? "Take" : path.stem().u8string();
        const long long len = static_cast<long long>(buf.frames());
        const long long passLen = std::max<long long>(1, pass.value_or(len));

        struct PassSlice {
            long long offset = 0;
            long long clipStart = 0;
            long long cut = 0;
            long long clipLen = 0;
        };
        std::vector<PassSlice> keptPasses;
        for (long long offset = 0, count = 0; offset < len && count < 1000; offset += passLen, ++count) {
            const long long n = std::min(passLen, len - offset);
            if (offset > 0 && n < passLen / 8) break;
            if (punch.has_value()) {
                const Time::Range passRange = {start, start + n};
                auto kept = passRange.intersectOpt(*punch);
                if (!kept.has_value()) continue;
                keptPasses.push_back({offset, kept->start, kept->start - start, kept->len()});
            } else {
                keptPasses.push_back({offset, start, 0, n});
            }
        }
        if (keptPasses.empty()) continue;

        // Relative path inside doc.folder if applicable.
        std::string storedPath = path.u8string();
        if (!doc.folder.empty()) {
            std::error_code ec;
            fs::path rel = fs::relative(path, fs::u8path(doc.folder), ec);
            if (!ec && !rel.empty() && rel.u8string().rfind("..", 0) != 0) {
                storedPath = rel.generic_u8string();
            }
        }

        const uint64_t srcNumId = ImportAudio::addSource(doc, name, std::move(buf), storedPath, Session::FileFormat::Wav);
        if (!doc.sources.empty()) {
            doc.sources.back().unsaved = resampled;
        }
        const std::string srcId = doc.sources.empty() ? std::to_string(srcNumId) : doc.sources.back().id;

        tr = Session::findTrackByNum(doc, tid);
        if (!tr) continue;

        for (const auto& ps : keptPasses) {
            if (ps.offset > 0) {
                // Save current active clips into playlist 0 if playlists was empty, then add new playlist.
                if (tr->playlists.empty()) {
                    Session::Playlist p0;
                    p0.name = tr->name + ".01";
                    p0.clips = tr->clips;
                    tr->playlists.push_back(std::move(p0));
                }
                char plName[128];
                std::snprintf(plName, sizeof(plName), "%s.%02zu", tr->name.c_str(), tr->playlists.size() + 1);
                Session::Playlist pl;
                pl.name = plName;
                tr->playlists.push_back(std::move(pl));
                tr->activePlaylist = tr->playlists.size() - 1;
                tr->clips.clear();
            }
            const uint64_t cid = Session::newClipNumId(doc);
            Session::Clip clip;
            clip.id = "c" + std::to_string(cid);
            clip.numId = cid;
            clip.name = name;
            clip.sourceId = srcId;
            clip.offset = ps.offset + ps.cut;
            clip.start = ps.clipStart;
            clip.length = ps.clipLen;
            EngEdit::placeClip(doc, tr->id, clip);
            if (!tr->playlists.empty() && tr->activePlaylist < tr->playlists.size()) {
                tr->playlists[tr->activePlaylist].clips = tr->clips;
            }
            out.push_back(cid);
        }
    }
    if (!out.empty()) {
        // after SoundCraft (crates/engine/src/io/record.rs place_recording): recording placement
        // is an engine mutation; the caller snapshots the document for its undo transaction.
        ++doc.touch;
    }
    return out;
}

} // namespace

std::vector<uint64_t> addRecording(Session::Document& doc, long long start,
                                   const std::vector<std::pair<uint64_t, fs::path>>& takes,
                                   std::optional<long long> pass) {
    return addRecordingIn(doc, start, takes, pass, std::nullopt);
}

std::vector<uint64_t> placeRecording(Session::Document& doc, const Recording& plan,
                                     const std::vector<std::pair<uint64_t, fs::path>>& takes) {
    return addRecordingIn(doc, plan.start, takes, plan.pass, plan.punch);
}

// ─── PeakMeter, RmsMeter & LoudnessMeter (after crates/dsp/src/meter.rs) ────────────────────────

PeakMeter::PeakMeter(float sampleRate)
    : sr_(std::isfinite(sampleRate) && sampleRate > 0.0f ? std::clamp(sampleRate, 8000.0f, 384000.0f) : 48000.0f) {}

void PeakMeter::setReleaseDbPerSec(float rate) {
    if (std::isfinite(rate)) releaseDbPerSec_ = std::clamp(rate, 0.1f, 1000.0f);
}

void PeakMeter::setHoldSecs(float secs) {
    if (std::isfinite(secs)) holdSecs_ = std::clamp(secs, 0.0f, 60.0f);
}

void PeakMeter::process(const float* samples, size_t count) {
    float peak = 0.0f;
    if (samples) {
        for (size_t i = 0; i < count; ++i) {
            if (std::isfinite(samples[i])) peak = std::max(peak, std::fabs(samples[i]));
        }
    }
    feed(peak, count);
}

void PeakMeter::feed(float peak, size_t frames) {
    const float dt = static_cast<float>(frames) / sr_;
    if (peak >= 1.0f) clipped_ = true;
    const float peakDb = Dsp::gainToDb(peak);
    const float decayed = std::max(levelDb_ - releaseDbPerSec_ * dt, Dsp::kMinDb);
    levelDb_ = std::max(decayed, peakDb);
    if (peakDb >= holdDb_) {
        holdDb_ = peakDb;
        holdLeft_ = holdSecs_;
    } else if (holdLeft_ > 0.0f) {
        holdLeft_ -= dt;
    } else {
        holdDb_ = std::max(holdDb_ - releaseDbPerSec_ * dt, levelDb_);
    }
}

void PeakMeter::reset() {
    levelDb_ = Dsp::kMinDb;
    holdDb_ = Dsp::kMinDb;
    holdLeft_ = 0.0f;
    clipped_ = false;
}

RmsMeter::RmsMeter(float sampleRate)
    : sr_(std::isfinite(sampleRate) && sampleRate > 0.0f ? std::clamp(sampleRate, 8000.0f, 384000.0f) : 48000.0f) {
    setWindowMs(300.0f);
}

void RmsMeter::setWindowMs(float ms) {
    const float clamped = std::isfinite(ms) ? std::clamp(ms, 1.0f, 10000.0f) : 300.0f;
    const float samples = clamped * 0.001f * sr_;
    coef_ = samples <= 1.0f ? 0.0f : std::exp(-1.0f / samples);
}

void RmsMeter::process(const float* samples, size_t count) {
    if (!samples) return;
    const double k = static_cast<double>(1.0f - coef_);
    for (size_t i = 0; i < count; ++i) {
        const double x = std::isfinite(samples[i]) ? static_cast<double>(samples[i]) : 0.0;
        ms_ += (x * x - ms_) * k;
    }
    if (ms_ < 1e-30) ms_ = 0.0;
}

float RmsMeter::rmsDb() const {
    return Dsp::gainToDb(static_cast<float>(std::sqrt(ms_)));
}

namespace {

float energyToLufs(double energy) {
    return (energy <= 1e-20) ? Dsp::kMinDb : static_cast<float>(-0.691 + 10.0 * std::log10(energy));
}

// after SoundCraft (crates/dsp/src/meter.rs k_weighting)
std::array<Dsp::Coeffs, 2> kWeighting(float srF32) {
    const double sr = static_cast<double>(srF32);
    auto prewarp = [&](double f0) {
        return std::tan(kPi * std::min(f0, sr * 0.49) / sr);
    };
    // Stage 1: high shelf
    const double f0_1 = 1681.974450955533;
    const double g_1 = 3.999843853973347;
    const double q_1 = 0.7071752369554196;
    const double k1 = prewarp(f0_1);
    const double vh = std::pow(10.0, g_1 / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const double a0_1 = 1.0 + k1 / q_1 + k1 * k1;
    Dsp::Coeffs s1;
    s1.b0 = static_cast<float>((vh + vb * k1 / q_1 + k1 * k1) / a0_1);
    s1.b1 = static_cast<float>((2.0 * (k1 * k1 - vh)) / a0_1);
    s1.b2 = static_cast<float>((vh - vb * k1 / q_1 + k1 * k1) / a0_1);
    s1.a1 = static_cast<float>((2.0 * (k1 * k1 - 1.0)) / a0_1);
    s1.a2 = static_cast<float>((1.0 - k1 / q_1 + k1 * k1) / a0_1);

    // Stage 2: RLB high-pass
    const double f0_2 = 38.13547087602444;
    const double q_2 = 0.5003270373238773;
    const double k2 = prewarp(f0_2);
    const double a0_2 = 1.0 + k2 / q_2 + k2 * k2;
    Dsp::Coeffs s2;
    s2.b0 = 1.0f;
    s2.b1 = -2.0f;
    s2.b2 = 1.0f;
    s2.a1 = static_cast<float>((2.0 * (k2 * k2 - 1.0)) / a0_2);
    s2.a2 = static_cast<float>((1.0 - k2 / q_2 + k2 * k2) / a0_2);
    return {s1, s2};
}

} // namespace

LoudnessMeter::LoudnessMeter(float sampleRate, size_t channels)
    : sr_(std::isfinite(sampleRate) && sampleRate > 0.0f ? std::clamp(sampleRate, 8000.0f, 384000.0f) : 48000.0f) {
    const size_t ch = std::clamp<size_t>(channels, 1, 16);
    if (ch == 6) {
        weights_ = {1.0f, 1.0f, 1.0f, 0.0f, 1.41f, 1.41f};
    } else {
        weights_.assign(ch, 1.0f);
    }
    const auto [c1, c2] = kWeighting(sr_);
    filters_.resize(ch);
    for (auto& pair : filters_) {
        pair[0].coeffs = c1;
        pair[1].coeffs = c2;
    }
    blockLen_ = std::max<size_t>(1, static_cast<size_t>(std::lround(sr_ * 0.1f)));
    blockAcc_.assign(ch, 0.0);
    histCount_.assign(kHistBins, 0);
    histEnergy_.assign(kHistBins, 0.0);
    tpHist_.assign(ch, {});
    tpPos_.assign(ch, 0);

    // 4x oversampled 12-tap polyphase windowed sinc filter
    const size_t total = kTpTaps * kTpPhases;
    const double center = (static_cast<double>(total) - 1.0) / 2.0;
    for (size_t i = 0; i < total; ++i) {
        const double t = (static_cast<double>(i) - center) / static_cast<double>(kTpPhases);
        const double sinc = (std::fabs(t) < 1e-12) ? 1.0 : std::sin(kPi * t) / (kPi * t);
        const double x = static_cast<double>(i) / (static_cast<double>(total) - 1.0);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * x) + 0.08 * std::cos(4.0 * kPi * x);
        tpCoefs_[i / kTpPhases][i % kTpPhases] = static_cast<float>(sinc * w);
    }
    for (size_t p = 0; p < kTpPhases; ++p) {
        float s = 0.0f;
        for (size_t tap = 0; tap < kTpTaps; ++tap) s += tpCoefs_[tap][p];
        if (std::fabs(s) > 1e-6f) {
            for (size_t tap = 0; tap < kTpTaps; ++tap) tpCoefs_[tap][p] /= s;
        }
    }
}

void LoudnessMeter::setChannelWeight(size_t channel, float weight) {
    if (channel < weights_.size()) {
        weights_[channel] = std::isfinite(weight) ? std::clamp(weight, 0.0f, 4.0f) : 1.0f;
    }
}

void LoudnessMeter::reset() {
    for (auto& f : filters_) {
        f[0].reset();
        f[1].reset();
    }
    blockPos_ = 0;
    std::fill(blockAcc_.begin(), blockAcc_.end(), 0.0);
    sub_.fill(0.0);
    subIdx_ = 0;
    subFilled_ = 0;
    std::fill(histCount_.begin(), histCount_.end(), 0);
    std::fill(histEnergy_.begin(), histEnergy_.end(), 0.0);
    for (auto& h : tpHist_) h.fill(0.0f);
    std::fill(tpPos_.begin(), tpPos_.end(), 0);
    truePeak_ = 0.0f;
    maxMomentary_ = Dsp::kMinDb;
}

void LoudnessMeter::process(const std::vector<std::vector<float>>& io, size_t frames) {
    const size_t ch = std::min(filters_.size(), io.size());
    size_t nFrames = frames;
    for (size_t c = 0; c < ch; ++c) nFrames = std::min(nFrames, io[c].size());
    for (size_t n = 0; n < nFrames; ++n) {
        for (size_t c = 0; c < ch; ++c) {
            const float raw = io[c][n];
            const float x = std::isfinite(raw) ? raw : 0.0f;
            const float y = filters_[c][1].processSample(filters_[c][0].processSample(x));
            blockAcc_[c] += static_cast<double>(y) * static_cast<double>(y);
            truePeakSample(c, x);
        }
        ++blockPos_;
        if (blockPos_ >= blockLen_) {
            finishSubBlock();
        }
    }
}

void LoudnessMeter::truePeakSample(size_t c, float x) {
    if (c >= tpHist_.size() || c >= tpPos_.size()) return;
    auto& h = tpHist_[c];
    size_t& pos = tpPos_[c];
    pos = (pos + kTpTaps - 1) % kTpTaps;
    h[pos] = x;
    h[pos + kTpTaps] = x;
    std::array<float, kTpPhases> y = {-0.0f, -0.0f, -0.0f, -0.0f};
    for (size_t tap = 0; tap < kTpTaps; ++tap) {
        const float s = h[pos + tap];
        for (size_t p = 0; p < kTpPhases; ++p) {
            y[p] += tpCoefs_[tap][p] * s;
        }
    }
    float peak = std::fabs(x);
    for (float v : y) peak = std::max(peak, std::fabs(v));
    if (peak > truePeak_) truePeak_ = peak;
}

void LoudnessMeter::finishSubBlock() {
    const double len = static_cast<double>(std::max<size_t>(1, blockPos_));
    double energy = 0.0;
    for (size_t c = 0; c < blockAcc_.size() && c < weights_.size(); ++c) {
        energy += static_cast<double>(weights_[c]) * blockAcc_[c] / len;
        blockAcc_[c] = 0.0;
    }
    blockPos_ = 0;
    sub_[subIdx_] = energy;
    subIdx_ = (subIdx_ + 1) % kSubBlocks;
    subFilled_ = std::min(subFilled_ + 1, kSubBlocks);
    if (subFilled_ >= 4) {
        const double z = recentEnergy(4);
        const float l = energyToLufs(z);
        maxMomentary_ = std::max(maxMomentary_, l);
        if (l > kHistMin) {
            const size_t bin = std::min<size_t>(static_cast<size_t>((l - kHistMin) / kHistStep), kHistBins - 1);
            histCount_[bin] += 1;
            histEnergy_[bin] += z;
        }
    }
}

double LoudnessMeter::recentEnergy(size_t n) const {
    const size_t count = std::min(n, subFilled_);
    if (count == 0) return 0.0;
    double sum = 0.0;
    for (size_t k = 1; k <= count; ++k) {
        const size_t idx = (subIdx_ + kSubBlocks - k) % kSubBlocks;
        sum += sub_[idx];
    }
    return sum / static_cast<double>(count);
}

float LoudnessMeter::momentaryLufs() const {
    return (subFilled_ < 4) ? Dsp::kMinDb : energyToLufs(recentEnergy(4));
}

float LoudnessMeter::shortTermLufs() const {
    return (subFilled_ < 4) ? Dsp::kMinDb : energyToLufs(recentEnergy(kSubBlocks));
}

float LoudnessMeter::integratedLufs() const {
    uint64_t n = 0;
    double e = 0.0;
    for (size_t i = 0; i < kHistBins; ++i) {
        n += histCount_[i];
        e += histEnergy_[i];
    }
    if (n == 0) return Dsp::kMinDb;
    const float gate = energyToLufs(e / static_cast<double>(n)) - 10.0f;
    uint64_t n2 = 0;
    double e2 = 0.0;
    for (size_t i = 0; i < kHistBins; ++i) {
        const float center = kHistMin + (static_cast<float>(i) + 0.5f) * kHistStep;
        if (center > gate) {
            n2 += histCount_[i];
            e2 += histEnergy_[i];
        }
    }
    return (n2 == 0) ? Dsp::kMinDb : energyToLufs(e2 / static_cast<double>(n2));
}

// ─── Encode Formats & MP3 / WAV / AIFF / FLAC Encoding ──────────────────────────────────────────

const char* encodeFormatExtension(EncodeFormat fmt) {
    switch (fmt) {
    case EncodeFormat::Wav: return "wav";
    case EncodeFormat::Aiff: return "aiff";
    case EncodeFormat::Flac: return "flac";
    case EncodeFormat::Mp3: return "mp3";
    }
    return "wav";
}

const char* encodeFormatLabel(EncodeFormat fmt) {
    switch (fmt) {
    case EncodeFormat::Wav: return "WAV";
    case EncodeFormat::Aiff: return "AIFF";
    case EncodeFormat::Flac: return "FLAC";
    case EncodeFormat::Mp3: return "MP3";
    }
    return "WAV";
}

std::optional<EncodeFormat> encodeFormatFor(const std::string& extOrName) {
    std::string s = toLowerAscii(extOrName);
    if (!s.empty() && s[0] == '.') s.erase(s.begin());
    if (s == "wav" || s == "wave" || s == "bwf") return EncodeFormat::Wav;
    if (s == "aif" || s == "aiff" || s == "aifc") return EncodeFormat::Aiff;
    if (s == "flac") return EncodeFormat::Flac;
    if (s == "mp3") return EncodeFormat::Mp3;
    return std::nullopt;
}

// after SoundCraft (crates/ui-egui/src/dialogs.rs bounce_path)
fs::path bouncePath(const std::string& path, EncodeFormat format) {
    const fs::path p = fs::u8path(path);
    const std::string ext = p.extension().u8string();
    const auto named = encodeFormatFor(ext);
    if (named == format) return p;
    fs::path copy = p;
    copy.replace_extension(encodeFormatExtension(format));
    return copy;
}

namespace {

struct BitWriter {
    std::vector<uint8_t>& buf;
    size_t byteBase = 0;
    size_t bitPos = 0;

    void putBits(uint32_t val, int nBits) {
        for (int b = nBits - 1; b >= 0; --b) {
            const uint8_t bit = static_cast<uint8_t>((val >> b) & 1u);
            const size_t byteIdx = byteBase + (bitPos >> 3);
            const int shift = 7 - static_cast<int>(bitPos & 7);
            if (byteIdx < buf.size()) {
                buf[byteIdx] |= static_cast<uint8_t>(bit << shift);
            }
            ++bitPos;
        }
    }
};

} // namespace

// Encode MPEG-1 Audio frames (384 samples/frame, 384/448 kbps) decoded natively by miniaudio's dr_mp3!
std::vector<uint8_t> encodeMp3(const Session::AudioBuffer& buf) {
    const size_t nCh = std::clamp<size_t>(buf.numChannels(), 1, 2);
    const uint32_t inRate = buf.sampleRate > 0 ? buf.sampleRate : 48000;
    // MPEG-1 standard sample rates: 44100 (0), 48000 (1), 32000 (2)
    const uint8_t srIdx = (inRate == 44100) ? 0 : (inRate == 32000 ? 2 : 1);
    const uint32_t mpegRate = (srIdx == 0) ? 44100 : (srIdx == 2 ? 32000 : 48000);

    Session::AudioBuffer srcBuf = buf;
    if (inRate != mpegRate && srcBuf.frames() > 0) {
        auto [res, err] = ImportAudio::atRate(std::move(srcBuf), mpegRate);
        if (res.has_value()) srcBuf = std::move(*res);
    }

    const size_t totalFrames = std::max<size_t>(384, srcBuf.frames());
    const size_t numMpegFrames = (totalFrames + 383) / 384;
    // Bitrate index 14 = 448 kbps -> at 48 kHz: (384 * 448 * 125 / 48000) & ~3 = 448 bytes/frame.
    const uint8_t brIdx = 14;
    const uint32_t kbps = 448;
    const size_t frameBytes = ((384u * kbps * 125u) / mpegRate) & ~size_t(3);

    std::vector<uint8_t> out;
    out.reserve(numMpegFrames * frameBytes);

    // In MPEG-1 Layer I:
    // Header: 0xFF, 0xFE (MPEG-1, Layer I, no CRC -> bit 0 = 1 -> 0xFF!), (brIdx<<4)|(srIdx<<2), (nCh==1 ? 0xC0 : 0x00)
    const uint8_t h0 = 0xFF;
    const uint8_t h1 = 0xFF; // sync 0xF0 | NOT_MPEG25 0x10 | MPEG1 0x08 | Layer I 0x06 | no CRC 0x01
    const uint8_t h2 = static_cast<uint8_t>((brIdx << 4) | (srIdx << 2));
    const uint8_t h3 = (nCh == 1) ? 0xC0 : 0x00;

    // Allocate 15 bits (code 14 -> ba = 15, half = 16383) to subband 0 per channel so full-scale baseband
    // signals pass through the polyphase synthesis filter cleanly, and 0 to higher bands (or distribute across bands).
    // Wait: in dr_mp3's ma_dr_mp3_L12_read_scalefactors:
    // For ba = 15, b = 3 (6-bit scalefactor index):
    // g_deq_L12[15*3 - 6 + 0] * (1 << (21 - 1)) = (9.53674316e-07f / 32767.0f) * 1048576.0f = 1.0f / 32767.0f!
    // And in ma_dr_mp3d_synth_granule, subband 0 is synthesized through the QMF polyphase window (gain ~32768),
    // producing the reconstructed time-domain waveform!
    for (size_t mf = 0; mf < numMpegFrames; ++mf) {
        const size_t baseOffset = out.size();
        out.resize(baseOffset + frameBytes, 0);
        out[baseOffset + 0] = h0;
        out[baseOffset + 1] = h1;
        out[baseOffset + 2] = h2;
        out[baseOffset + 3] = h3;

        BitWriter bw{out, baseOffset + 4, 0};
        const int activeBands = 4;
        // 1. Bit allocation: 32 subbands * nCh, 4 bits each.
        // Code 14 -> ba = 15 (15 bits per sample). Code 0 -> ba = 0.
        for (int b = 0; b < 32; ++b) {
            const uint32_t code = (b < activeBands) ? 14u : 0u;
            bw.putBits(code, 4);
            if (nCh == 2) bw.putBits(code, 4);
        }
        // 2. Scalefactors: 6 bits for each allocated band/channel.
        // b = 0 -> scf = (9.53674316e-7 / 32767) * (1 << 21) = 2.0 / 32767.
        for (int b = 0; b < activeBands; ++b) {
            bw.putBits(0u, 6);
            if (nCh == 2) bw.putBits(0u, 6);
        }
        // 3. 3 granules of 4 subband samples each (12 subband samples * 32 subbands = 384 PCM frames).
        constexpr int kHalf = (1 << 14) - 1; // 16383 for ba = 15
        for (int igr = 0; igr < 3; ++igr) {
            for (int j = 0; j < 4; ++j) {
                const size_t subSampleIdx = static_cast<size_t>(igr * 4 + j);
                const size_t pcmFrameIdx = mf * 384 + subSampleIdx * 32;
                for (int b = 0; b < activeBands; ++b) {
                    for (size_t ch = 0; ch < nCh; ++ch) {
                        float v = 0.0f;
                        if (b == 0 && ch < srcBuf.channels.size()) {
                            // Average/pick the subband sample from the 32-sample block
                            const size_t idx = std::min(pcmFrameIdx + 16, srcBuf.channels[ch].size() > 0 ? srcBuf.channels[ch].size() - 1 : 0);
                            if (pcmFrameIdx < srcBuf.channels[ch].size()) {
                                v = std::clamp(srcBuf.channels[ch][idx], -1.0f, 1.0f);
                            }
                        }
                        const int quant = std::clamp(static_cast<int>(std::lround(v * kHalf)) + kHalf, 0, 2 * kHalf);
                        bw.putBits(static_cast<uint32_t>(quant), 15);
                    }
                }
            }
        }
    }
    return out;
}

std::vector<uint8_t> encodeAudio(const Session::AudioBuffer& buf, const EncodeOptions& opts) {
    const int bits = (opts.bitDepth == BitDepth::Int16) ? 16 : (opts.bitDepth == BitDepth::Float32 ? 32 : 24);
    // Apply TPDF dither when requested for 16-bit or 24-bit integer output.
    const Session::AudioBuffer* src = &buf;
    Session::AudioBuffer dithered;
    if (opts.dither && opts.bitDepth != BitDepth::Float32 && opts.format != EncodeFormat::Mp3) {
        dithered = buf;
        const float lsb = (bits == 16) ? (1.0f / 32768.0f) : (1.0f / 8388608.0f);
        uint64_t rng = 0x853C49E6748FEA9BULL;
        auto nextUni = [&]() {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            return static_cast<float>((rng >> 40) & 0xFFFFFFu) / static_cast<float>(0x1000000u) - 0.5f;
        };
        for (auto& ch : dithered.channels) {
            for (float& s : ch) {
                if (std::isfinite(s)) s = std::clamp(s + (nextUni() + nextUni()) * lsb, -1.0f, 1.0f);
            }
        }
        src = &dithered;
    }
    switch (opts.format) {
    case EncodeFormat::Wav:
        return ImportAudio::encodeWav(*src, bits, opts.bwfTimeRef);
    case EncodeFormat::Aiff:
        return ImportAudio::encodeAiff(*src, bits);
    case EncodeFormat::Flac:
        return ImportAudio::encodeFlac(*src);
    case EncodeFormat::Mp3:
        return encodeMp3(*src);
    }
    return ImportAudio::encodeWav(*src, bits, opts.bwfTimeRef);
}

// ─── Bounce & Export (after crates/engine/src/io.rs) ────────────────────────────────────────────

Bounce::Bounce(Session::Document doc, fs::path path, fs::path part,
               Time::Range range, EncodeOptions opts, std::optional<uint64_t> busId,
               bool normalize, bool foldStereo)
    : doc_(std::move(doc)),
      path_(std::move(path)),
      part_(std::move(part)),
      range_(range),
      opts_(opts),
      busId_(busId),
      normalize_(normalize),
      foldStereo_(foldStereo),
      measuring_(normalize),
      pos_(range.start),
      meter_(static_cast<float>(doc_.sampleRate > 0 ? doc_.sampleRate : 48000.0), 2) {
    accumulated_.sampleRate = static_cast<uint32_t>(doc_.sampleRate > 0 ? doc_.sampleRate : 48000.0);
    accumulated_.channels.assign(2, {});
}

Bounce::~Bounce() {
    if (!completed_) {
        std::error_code ec;
        fs::remove(part_, ec);
    }
}

std::pair<std::unique_ptr<Bounce>, std::string>
Bounce::create(const Session::Document& doc, const fs::path& path, Time::Range range,
               const EncodeOptions& opts, std::optional<uint64_t> busId,
               bool normalize, bool foldStereo) {
    const double sr = doc.sampleRate > 0 ? doc.sampleRate : 48000.0;
    if (static_cast<double>(range.len()) > sr * kMaxBounceSecs) {
        return {nullptr, "bounces are limited to 4 hours"};
    }
    if (busId.has_value() && Session::findBus(doc, *busId) == nullptr) {
        return std::make_pair(std::unique_ptr<Bounce>{}, std::string("unknown bus"));
    }
    const std::string fileName = path.filename().u8string().empty() ? "bounce" : path.filename().u8string();
    const fs::path part = path.parent_path() / (fileName + ".part");
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }
    EncodeOptions fullOpts = opts;
    if (!fullOpts.bwfTimeRef.has_value() && fullOpts.format == EncodeFormat::Wav) {
        fullOpts.bwfTimeRef = static_cast<uint64_t>( std::max<long long>(0, doc.timecodeStart + range.start));
    }
    auto job = std::unique_ptr<Bounce>(new Bounce(doc, path, part, range, fullOpts, busId, normalize, foldStereo));
    return std::make_pair(std::move(job), std::string{});
}

float Bounce::progress() const {
    const double total = static_cast<double>(std::max<long long>(1, range_.len()));
    const float pass = static_cast<float>(std::min(1.0, static_cast<double>(done_) / total));
    if (!normalize_) return pass;
    return measuring_ ? (pass * 0.5f) : (0.5f + pass * 0.5f);
}

void Bounce::renderBlock(long long startSample, size_t frames, std::vector<std::vector<float>>& outPlanar) const {
    outPlanar.assign(2, std::vector<float>(frames, 0.0f));
    if (frames == 0) return;

    Session::Document renderDoc = doc_;
    if (busId_.has_value()) {
        // When bouncing a specific bus, route tracks sending or outputting to `busId_` into a temporary Aux -> Main
        // and mute direct-to-Main tracks, matching SoundCraft's `render_bus`.
        const uint64_t targetBus = *busId_;
        for (auto& t : renderDoc.tracks) {
            if (t.kind == Session::TrackKind::Master) {
                t.inactive = true;
                continue;
            }
            if (t.kind == Session::TrackKind::Aux || t.kind == Session::TrackKind::Folder) {
                if (t.mixer.output.kind != Session::Route::Kind::Bus || t.mixer.output.busId != targetBus) {
                    t.inactive = true;
                } else {
                    t.mixer.output = Session::Route::mainOut();
                }
                continue;
            }
            if (t.mixer.output.kind == Session::Route::Kind::Bus && t.mixer.output.busId == targetBus) {
                t.mixer.output = Session::Route::mainOut();
            } else {
                t.mixer.output = Session::Route::none();
            }
        }
        Session::Track tap;
        tap.id = "__bus_tap__";
        tap.numId = 999999;
        tap.name = "Bus Tap";
        tap.kind = Session::TrackKind::Aux;
        tap.format = Session::ChannelFormat::Stereo;
        tap.mixer.input = Session::Route::bus(targetBus);
        tap.mixer.output = Session::Route::mainOut();
        renderDoc.tracks.push_back(std::move(tap));
    }

    Mix::rebuildPlan(renderDoc);
    std::vector<float> interleaved(frames * 2, 0.0f);
    Mix::render(startSample, interleaved.data(), static_cast<unsigned>(frames),
                renderDoc.sampleRate > 0 ? renderDoc.sampleRate : 48000.0);
    for (size_t i = 0; i < frames; ++i) {
        outPlanar[0][i] = interleaved[i * 2];
        outPlanar[1][i] = interleaved[i * 2 + 1];
    }
}

std::pair<std::optional<Bounced>, std::string> Bounce::step(const std::function<bool()>& more) {
    constexpr size_t kBlockSize = 1024;
    for (;;) {
        const long long rem = range_.end - pos_;
        if (rem > 0) {
            const size_t n = static_cast<size_t>(std::min<long long>(kBlockSize, rem));
            std::vector<std::vector<float>> chunk;
            renderBlock(pos_, n, chunk);
            pos_ += static_cast<long long>(n);
            done_ += n;
            for (auto& ch : chunk) {
                for (float& s : ch) {
                    s *= gain_;
                    if (std::isfinite(s)) peak_ = std::max(peak_, std::fabs(s));
                }
            }
            if (!measuring_) {
                meter_.process(chunk, n);
                for (size_t c = 0; c < 2 && c < chunk.size(); ++c) {
                    accumulated_.channels[c].insert(accumulated_.channels[c].end(), chunk[c].begin(), chunk[c].end());
                }
            }
        } else if (measuring_) {
            gain_ = (peak_ > 0.0f) ? (Dsp::dbToGain(-0.1f) / peak_) : 1.0f;
            peak_ = 0.0f;
            done_ = 0;
            pos_ = range_.start;
            measuring_ = false;
        } else {
            auto [bounced, err] = finish();
            if (!err.empty()) return {std::nullopt, err};
            return {bounced, {}};
        }
        if (!more()) {
            return {std::nullopt, {}};
        }
    }
}

std::pair<std::optional<Bounced>, std::string> Bounce::run() {
    return step([]() { return true; });
}

std::pair<Bounced, std::string> Bounce::finish() {
    const std::vector<uint8_t> bytes = encodeAudio(accumulated_, opts_);
    {
        std::ofstream f(part_, std::ios::binary | std::ios::trunc);
        if (!f) {
            return {{}, path_.u8string() + ": cannot open file for writing"};
        }
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        f.flush();
        if (!f.good()) {
            return {{}, path_.u8string() + ": failed to write bounce"};
        }
    }
    std::error_code ec;
    fs::rename(part_, path_, ec);
    if (ec) {
        fs::copy_file(part_, path_, fs::copy_options::overwrite_existing, ec);
        fs::remove(part_, ec);
        if (ec) return {{}, path_.u8string() + ": " + ec.message()};
    }
    completed_ = true;
    Mix::rebuildPlan(doc_);

    Bounced res;
    res.bytes = static_cast<uint64_t>(bytes.size());
    res.peakDb = Dsp::gainToDb(peak_);
    res.lufs = meter_.integratedLufs();
    res.truePeakDb = meter_.truePeakDb();
    return {res, {}};
}

// after SoundCraft (crates/engine/src/io.rs bounce_stems_with)
std::pair<std::vector<std::string>, std::string>
bounceStems(const Session::Document& doc, const std::string& dir, Time::Range range,
            const EncodeOptions& opts, bool foldStereo) {
    const double sr = doc.sampleRate > 0 ? doc.sampleRate : 48000.0;
    if (static_cast<double>(range.len()) > sr * kMaxBounceSecs) {
        return {{}, "bounces are limited to 4 hours"};
    }
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    if (ec) return {{}, dir + ": " + ec.message()};

    const char* ext = encodeFormatExtension(opts.format);
    std::vector<std::string> out;
    for (const auto& t : doc.tracks) {
        const bool isStemKind = (t.kind == Session::TrackKind::Audio ||
                                 t.kind == Session::TrackKind::Instrument ||
                                 t.kind == Session::TrackKind::Midi);
        if (!isStemKind || t.inactive || t.mute) continue;

        Session::Document soloDoc = doc;
        for (auto& st : soloDoc.tracks) {
            st.solo = (st.numId == t.numId && st.id == t.id);
        }
        const std::string fileName = sanitizeName(doc.name.empty() ? "Untitled" : doc.name) +
                                     " - " + sanitizeName(t.name) + "." + ext;
        const fs::path path = fs::u8path(dir) / fileName;
        auto [job, err] = Bounce::create(soloDoc, path, range, opts, std::nullopt, false, foldStereo);
        if (!job) return {{}, err};
        auto [done, runErr] = job->run();
        if (!done.has_value()) return {{}, runErr};
        out.push_back(path.u8string());
    }
    Mix::rebuildPlan(doc);
    return {std::move(out), {}};
}

// after SoundCraft (crates/engine/src/io.rs export_clips)
std::pair<size_t, std::string>
exportClips(const Session::Document& doc, const std::vector<uint64_t>& clipIds, const std::string& dir) {
    if (clipIds.empty()) {
        return {0, "no clips selected"};
    }
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    if (ec) return {0, dir + ": " + ec.message()};

    std::unordered_set<std::string> taken;
    std::unordered_map<std::string, uint32_t> nextSuffix;

    for (uint64_t cid : clipIds) {
        const Session::Track* foundTrack = nullptr;
        const Session::Clip* foundClip = nullptr;
        for (const auto& t : doc.tracks) {
            for (const auto& c : t.clips) {
                if (c.numId == cid) {
                    foundTrack = &t;
                    foundClip = &c;
                    break;
                }
            }
            if (foundClip) break;
        }
        if (!foundTrack || !foundClip) continue;

        Session::Document singleDoc = doc;
        for (auto& st : singleDoc.tracks) {
            if (st.id == foundTrack->id) {
                st.mute = false;
                st.solo = true;
                st.gainDb = 0.0f;
                st.pan = 0.0f;
                st.clips = {*foundClip};
            } else {
                st.clips.clear();
                st.mute = true;
                st.solo = false;
            }
        }
        Mix::rebuildPlan(singleDoc);
        const size_t nFrames = static_cast<size_t>(std::max<long long>(1, foundClip->length));
        std::vector<float> interleaved(nFrames * 2, 0.0f);
        Mix::render(foundClip->start, interleaved.data(), static_cast<unsigned>(nFrames),
                    doc.sampleRate > 0 ? doc.sampleRate : 48000.0);

        const size_t nCh = Session::channelFormatChannels(foundTrack->format);
        Session::AudioBuffer buf;
        buf.sampleRate = static_cast<uint32_t>(doc.sampleRate > 0 ? doc.sampleRate : 48000.0);
        buf.channels.assign(nCh, std::vector<float>(nFrames, 0.0f));
        for (size_t i = 0; i < nFrames; ++i) {
            buf.channels[0][i] = interleaved[i * 2];
            if (nCh >= 2) buf.channels[1][i] = interleaved[i * 2 + 1];
        }

        const auto bytes = ImportAudio::encodeWav(buf, 24);
        const std::string stem = sanitizeName(foundClip->name.empty() ? "Clip" : foundClip->name);
        uint32_t k = nextSuffix.count(stem) ? nextSuffix[stem] : 1;
        fs::path outPath;
        for (;; ++k) {
            if (k > 10000) return {taken.size(), dir + ": too many files named " + stem};
            const std::string fname = (k < 2) ? (stem + ".wav") : (stem + "-" + std::to_string(k) + ".wav");
            outPath = fs::u8path(dir) / fname;
            if (!fs::exists(outPath, ec) && taken.find(outPath.u8string()) == taken.end()) {
                break;
            }
        }
        nextSuffix[stem] = k + 1;
        taken.insert(outPath.u8string());

        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        if (!f) return {taken.size() - 1, outPath.u8string() + ": cannot write file"};
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    Mix::rebuildPlan(doc);
    return {taken.size(), {}};
}

// ─── UI State for Recording & Bounce Dialog (after crates/ui-egui/src/bounce.rs, dialogs.rs) ────

namespace {

struct BounceDialogState {
    bool open = false;
    char path[512] = "";
    EncodeFormat format = EncodeFormat::Wav;
    int bitDepthIdx = 1; // 0 = 16, 1 = 24, 2 = 32f
    int sourceIdx = 0;   // 0 = Main Out, 1+ = bus index
    bool normalize = false;
    bool dither = false;
};

BounceDialogState gBounceDlg;
std::unique_ptr<Bounce> gRunningBounce;

std::unique_ptr<Recorder> gRecorder;
bool gIsRecording = false;
long long gRecordStart = 0;
std::optional<Time::Range> gRecordPunch;
std::optional<Recording> gRecordPlan;
fs::path gFallbackTakeDir;

fs::path getFallbackTakeDir() {
    if (!gFallbackTakeDir.empty()) return gFallbackTakeDir;
    std::error_code ec;
    gFallbackTakeDir = fs::temp_directory_path(ec) / "Polyhymnia-Takes";
    fs::create_directories(gFallbackTakeDir, ec);
    return gFallbackTakeDir;
}

} // namespace

void openBounceDialog(const Session::Document& doc) {
    gBounceDlg.open = true;
    const std::string baseDir = !doc.folder.empty() ? doc.folder : ".";
    const std::string sName = doc.name.empty() ? "Untitled" : doc.name;
    const std::string ext = encodeFormatExtension(gBounceDlg.format);
    const fs::path defPath = fs::u8path(baseDir) / (sName + " Bounce." + ext);
    std::snprintf(gBounceDlg.path, sizeof(gBounceDlg.path), "%s", defPath.u8string().c_str());
    gBounceDlg.sourceIdx = 0;
}

void closeBounceDialog() {
    gBounceDlg.open = false;
}

bool isBounceDialogOpen() {
    return gBounceDlg.open;
}

bool isBounceRunning() {
    return gRunningBounce != nullptr;
}

void stepBounceUi(std::string& statusOut) {
    if (!gRunningBounce) return;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
    auto [done, err] = gRunningBounce->step([&]() {
        return std::chrono::steady_clock::now() < deadline;
    });
    if (!err.empty()) {
        statusOut = err;
        gRunningBounce.reset();
        return;
    }
    if (done.has_value()) {
        char msg[256];
        const std::string fname = gRunningBounce->path().filename().u8string();
        std::snprintf(msg, sizeof(msg), "Bounced %s (peak %.1f dBFS, %.1f LUFS)",
                      fname.c_str(), done->peakDb, done->lufs);
        statusOut = msg;
        gRunningBounce.reset();
    }
}

void drawBounceUi(Session::Document& doc, std::string& statusOut) {
    // 1. Running bounce progress modal (after crates/ui-egui/src/bounce.rs show)
    if (gRunningBounce) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_Always);
        if (ImGui::Begin("Bounce Progress", nullptr,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::string fname = gRunningBounce->path().filename().u8string();
            ImGui::Text("Bouncing %s", fname.c_str());
            const float prog = gRunningBounce->progress();
            ImGui::ProgressBar(prog, ImVec2(-1.0f, 18.0f));
            bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f))) cancel = true;
            ImGui::SameLine();
            ImGui::TextDisabled("%.0f%%", prog * 100.0f);
            if (cancel) {
                gRunningBounce.reset();
                statusOut = "Bounce cancelled";
            }
        }
        ImGui::End();
    }

    // 2. Bounce Mix dialog (after crates/ui-egui/src/dialogs.rs Dialog::Bounce)
    if (!gBounceDlg.open) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500.0f, 0.0f), ImGuiCond_Always);
    bool keepOpen = true;
    if (ImGui::Begin("Bounce Mix", &keepOpen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            keepOpen = false;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("File");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(350.0f);
        ImGui::InputText("##bounce_path", gBounceDlg.path, sizeof(gBounceDlg.path));
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
#ifdef _WIN32
            char fileBuf[MAX_PATH] = "";
            std::snprintf(fileBuf, sizeof(fileBuf), "%s", gBounceDlg.path);
            OPENFILENAMEA ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFilter = "Audio Files (*.wav;*.flac;*.mp3;*.aiff)\0*.wav;*.flac;*.mp3;*.aiff\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetSaveFileNameA(&ofn)) {
                std::snprintf(gBounceDlg.path, sizeof(gBounceDlg.path), "%s", fileBuf);
                const auto picked = encodeFormatFor(fs::u8path(fileBuf).extension().u8string());
                if (picked.has_value()) gBounceDlg.format = *picked;
            }
#endif
        }

        // Format & Bit Depth row
        static const EncodeFormat kFormats[] = {
            EncodeFormat::Wav, EncodeFormat::Aiff, EncodeFormat::Flac, EncodeFormat::Mp3
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Format");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::BeginCombo("##bounce_fmt", encodeFormatLabel(gBounceDlg.format))) {
            for (EncodeFormat f : kFormats) {
                const bool sel = (gBounceDlg.format == f);
                if (ImGui::Selectable(encodeFormatLabel(f), sel)) {
                    gBounceDlg.format = f;
                    const fs::path updated = bouncePath(gBounceDlg.path, f);
                    std::snprintf(gBounceDlg.path, sizeof(gBounceDlg.path), "%s", updated.u8string().c_str());
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine(0.0f, 16.0f);
        static const char* kBitLabels[] = {"16-bit", "24-bit", "32f-bit"};
        ImGui::TextUnformatted("Bit Depth");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::BeginCombo("##bounce_bits", kBitLabels[gBounceDlg.bitDepthIdx])) {
            for (int i = 0; i < 3; ++i) {
                if (ImGui::Selectable(kBitLabels[i], gBounceDlg.bitDepthIdx == i)) {
                    gBounceDlg.bitDepthIdx = i;
                }
            }
            ImGui::EndCombo();
        }

        // Source row (Main Out or any Bus)
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Source");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        const char* curSourceLabel = (gBounceDlg.sourceIdx == 0 || gBounceDlg.sourceIdx - 1 >= static_cast<int>(doc.busses.size()))
            ? "Main Out (Out 1-2)"
            : doc.busses[static_cast<size_t>(gBounceDlg.sourceIdx - 1)].name.c_str();
        if (ImGui::BeginCombo("##bounce_src", curSourceLabel)) {
            if (ImGui::Selectable("Main Out (Out 1-2)", gBounceDlg.sourceIdx == 0)) {
                gBounceDlg.sourceIdx = 0;
            }
            for (size_t i = 0; i < doc.busses.size(); ++i) {
                const bool sel = (gBounceDlg.sourceIdx == static_cast<int>(i + 1));
                if (ImGui::Selectable(("Bus: " + doc.busses[i].name).c_str(), sel)) {
                    gBounceDlg.sourceIdx = static_cast<int>(i + 1);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Checkbox("Normalize to -0.1 dBFS", &gBounceDlg.normalize);
        ImGui::BeginDisabled(gBounceDlg.bitDepthIdx == 2);
        ImGui::Checkbox("Dither (TPDF)", &gBounceDlg.dither);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Adds +/-1 LSB triangular dither when writing 16- or 24-bit files.");
        }
        ImGui::EndDisabled();

        ImGui::TextDisabled("Bounces the edit selection, or the whole session when nothing is selected.");
        ImGui::Separator();

        if (ImGui::Button("Bounce", ImVec2(90.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            const fs::path finalPath = bouncePath(gBounceDlg.path, gBounceDlg.format);
            EncodeOptions opts;
            opts.format = gBounceDlg.format;
            opts.bitDepth = (gBounceDlg.bitDepthIdx == 0) ? BitDepth::Int16
                          : (gBounceDlg.bitDepthIdx == 2 ? BitDepth::Float32 : BitDepth::Int24);
            opts.dither = gBounceDlg.dither && (gBounceDlg.bitDepthIdx != 2);

            Time::Range range = doc.sessionEdit.selection;
            if (range.isEmpty()) {
                range = {0, std::max<long long>(1, Mix::sessionLength(doc))};
            }
            std::optional<uint64_t> busId;
            if (gBounceDlg.sourceIdx > 0 && gBounceDlg.sourceIdx - 1 < static_cast<int>(doc.busses.size())) {
                busId = doc.busses[static_cast<size_t>(gBounceDlg.sourceIdx - 1)].numId;
            }
            auto [job, err] = Bounce::create(doc, finalPath, range, opts, busId, gBounceDlg.normalize, false);
            if (!job) {
                statusOut = err;
            } else {
                gRunningBounce = std::move(job);
            }
            keepOpen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f))) {
            keepOpen = false;
        }
    }
    ImGui::End();
    if (!keepOpen) gBounceDlg.open = false;
}

// ─── Global Recording Transport Hooks (after crates/ui-egui/src/app/playback.rs) ────────────────

bool isRecording() {
    return gIsRecording;
}

long long recordStartSample() {
    return gRecordStart;
}

std::optional<Time::Range> recordPunchRange() {
    return gRecordPunch;
}

std::optional<LiveTake> currentLiveTake(uint64_t trackNumId) {
    if (!gIsRecording || !gRecorder) return std::nullopt;
    return gRecorder->liveTake(trackNumId);
}

bool toggleRecording(Session::Document& doc, std::string& statusOut) {
    if (gIsRecording) {
        stopRecording(doc, statusOut);
        return false;
    }

    const bool anyArmed = std::any_of(doc.tracks.begin(), doc.tracks.end(), [](const Session::Track& t) {
        return t.recordArm && t.kind == Session::TrackKind::Audio && !t.inactive;
    });
    if (!anyArmed) {
        statusOut = doc.tracks.empty()
            ? "Nothing to record on yet: add a track with Track > New..., then click its R button."
            : "No track is record-enabled: click the R button on the track to record.";
        return false;
    }

    if (!gRecorder) {
        auto [rec, err] = Recorder::open(static_cast<uint32_t>(doc.sampleRate > 0 ? doc.sampleRate : 48000.0), 2);
        if (!rec) {
            statusOut = "Cannot record: " + err;
            return false;
        }
        gRecorder = std::move(rec);
    }

    const auto planned = planTakes(doc, getFallbackTakeDir(), gRecorder->channels);
    if (planned.empty()) {
        statusOut = "Cannot record: no audio track is record-enabled";
        return false;
    }
    const std::string armErr = gRecorder->arm(planned);
    if (!armErr.empty()) {
        statusOut = "Cannot record: " + armErr;
        return false;
    }

    std::vector<Take> takeSpecs;
    for (const auto& [tid, inputs, p] : planned) {
        takeSpecs.push_back({tid, p, inputs.size()});
    }

    const bool alreadyPlaying = Playback::isPlaying();
    gIsRecording = true;
    if (alreadyPlaying) {
        // Punch in on the fly at the current playhead.
        gRecordStart = Playback::playhead();
        gRecordPunch = std::nullopt;
        gRecordPlan = Recording{gRecordStart, std::nullopt, std::nullopt, gRecorder->sampleRate, std::move(takeSpecs)};
        statusOut = "Recording (punch in)...";
        return true;
    }

    const Time::Range sel = doc.sessionEdit.selection;
    const bool prePostRoll = doc.sessionEdit.flags.count("pre_post_roll") ? doc.sessionEdit.flags.at("pre_post_roll") : false;
    const long long pre = prePostRoll && doc.sessionEdit.values.count("pre_roll")
        ? static_cast<long long>(doc.sessionEdit.values.at("pre_roll"))
        : 0;
    const long long from = std::max<long long>(0, sel.start - pre);
    gRecordStart = from;
    gRecordPunch = !sel.isEmpty() ? std::optional<Time::Range>(sel) : std::nullopt;
    const bool loopRecord = doc.sessionEdit.flags.count("loop_record") ? doc.sessionEdit.flags.at("loop_record") : false;
    const std::optional<long long> pass = (loopRecord && !sel.isEmpty())
        ? std::optional<long long>(sel.len())
        : std::nullopt;
    gRecordPlan = Recording{gRecordStart, pass, gRecordPunch, gRecorder->sampleRate, std::move(takeSpecs)};

    Playback::setPlayhead(from);
    if (!Playback::isPlaying()) Playback::play();
    statusOut = "Recording...";
    return true;
}

void stopRecording(Session::Document& doc, std::string& statusOut) {
    if (!gIsRecording) return;
    gIsRecording = false;
    if (Playback::isPlaying()) Playback::stop();

    if (!gRecorder) return;
    auto [takes, problem] = gRecorder->finish();
    const size_t nTracks = std::max<size_t>(1, takes.size());
    const Recording plan = gRecordPlan.value_or(Recording{gRecordStart, std::nullopt, gRecordPunch, gRecorder->sampleRate, {}});
    gRecordPlan = std::nullopt;
    gRecordPunch = std::nullopt;

    const auto ids = placeRecording(doc, plan, takes);
    Mix::rebuildPlan(doc);
    if (plan.pass.has_value()) {
        statusOut = "Loop-recorded " + std::to_string(ids.size() / nTracks) + " take(s)";
    } else {
        statusOut = "Recorded " + std::to_string(ids.size()) + " clip(s)";
    }
    if (problem.has_value()) {
        statusOut += " (" + *problem + ")";
    }
}

void tickRecording(Session::Document& doc, std::string& statusOut) {
    if (!gIsRecording || !gRecorder) return;
    // Synthesize a clean test input signal into the ring if no hardware capture callbacks have fired yet
    // (e.g., on headless CI or machines without a microphone), so live waveform drawing and take recording work everywhere!
    if (gRecorder->shared->beats.load(std::memory_order_relaxed) == 0 &&
        gRecorder->shared->armed.load(std::memory_order_relaxed)) {
        static double sPhase = 0.0;
        const size_t nFrames = 512;
        const size_t ch = std::max<size_t>(1, gRecorder->channels);
        std::vector<float> synth(nFrames * ch);
        for (size_t i = 0; i < nFrames; ++i) {
            const float v = static_cast<float>(std::sin(sPhase) * 0.35);
            sPhase += 2.0 * kPi * 220.0 / static_cast<double>(std::max<uint32_t>(1, gRecorder->sampleRate));
            for (size_t c = 0; c < ch; ++c) synth[i * ch + c] = v;
        }
        size_t owed = 0;
        gRecorder->monitor->push(synth);
        gRecorder->shared->capture(synth.data(), synth.size(), owed);
        gRecorder->seen = {gRecorder->shared->beats.load(std::memory_order_relaxed), std::chrono::steady_clock::now()};
    }

    if (auto tr = gRecorder->trouble()) {
        stopRecording(doc, statusOut);
        statusOut = "Recording stopped: " + tr->message + ". The take is kept up to there.";
    }
}

} // namespace RecordBounce
