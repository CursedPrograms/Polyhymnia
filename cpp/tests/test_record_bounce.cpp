// Polyhymnia - Unit tests for Recording, Input Monitoring, Punch In/Out, LoudnessMeter, and Bounce/Export
// after SoundCraft (crates/playback/src/record.rs, crates/engine/src/io/record.rs, crates/dsp/src/meter.rs,
// crates/engine/src/cmd/file/tests.rs).
#include "engine.h"
#include "engine_edit.h"
#include "import_audio.h"
#include "record_bounce.h"
#include "session.h"
#include "test_runner.h"
#include "undo.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

static std::vector<std::vector<float>> decodeTake(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ImportAudio::AudioInfo info;
    Session::AudioBuffer buf;
    std::string err;
    if (!ImportAudio::decodeBytes(bytes.data(), bytes.size(), path.extension().u8string(), info, buf, err)) {
        return {};
    }
    return buf.channels;
}

static void writeTakeFile(const fs::path& path, size_t frames) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    std::vector<uint8_t> bytes = RecordBounce::wavFloatHeader(48000, 1, frames);
    for (size_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(std::max<size_t>(1, frames)) - 0.5f;
        uint32_t u = 0;
        std::memcpy(&u, &v, sizeof(float));
        bytes.push_back(static_cast<uint8_t>(u & 0xFF));
        bytes.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

static uint64_t createArmedTrack(EngCmd::Engine& e, const char* name, const char* format, const char* input) {
    EngCmd::Engine::Params pNew;
    pNew.strs["name"] = name;
    pNew.strs["format"] = format;
    e.execute("track.new", pNew);
    const uint64_t tid = e.doc.tracks.back().numId;
    EngCmd::Engine::Params pIn;
    pIn.idLists["tracks"] = {tid};
    pIn.strs["input"] = input;
    e.execute("track.input", pIn);
    EngCmd::Engine::Params pArm;
    pArm.idLists["tracks"] = {tid};
    pArm.bools["value"] = true;
    e.execute("mix.record_arm", pArm);
    return tid;
}

// after SoundCraft (crates/playback/src/record.rs a_full_ring_owes_the_lost_samples_as_silence)
static bool testFullRingOwesLostSamplesAsSilence() {
    RecordBounce::SharedCapture shared(8);
    size_t owed = 0;
    std::vector<float> out;
    const std::vector<float> b1(6, 1.0f);
    const std::vector<float> b2(4, 2.0f);
    const std::vector<float> b3(2, 3.0f);
    shared.capture(b1.data(), b1.size(), owed);
    shared.capture(b2.data(), b2.size(), owed);
    shared.ring.popAll(out);
    shared.capture(b3.data(), b3.size(), owed);
    shared.ring.popAll(out);

    const std::vector<float> want = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 0.0f, 0.0f, 3.0f, 3.0f};
    EXPECT_EQ(out, want);
    EXPECT_EQ(owed, size_t(0));
    EXPECT_EQ(shared.dropped.load(), size_t(2));
    return true;
}

// after SoundCraft (crates/playback/src/record.rs nan_and_infinite_input_records_as_silence)
static bool testNanAndInfiniteInputRecordsAsSilence() {
    RecordBounce::SharedCapture shared(8);
    size_t owed = 0;
    std::vector<float> out;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float in[] = {0.5f, nan, inf, -inf, -0.5f};
    shared.capture(in, 5, owed);
    shared.ring.popAll(out);
    const std::vector<float> want = {0.5f, 0.0f, 0.0f, 0.0f, -0.5f};
    EXPECT_EQ(out, want);
    return true;
}

// after SoundCraft (crates/playback/src/record.rs a_bug_a_stall_or_a_lost_device_is_trouble)
static bool testABugAStallOrALostDeviceIsTrouble() {
    auto shared = std::make_shared<RecordBounce::SharedCapture>(64);
    auto mon = std::make_shared<RecordBounce::InputRing>(1, 48000);
    RecordBounce::Recorder r(shared, mon, "Mic In", 48000, 1);

    shared->armed.store(true);
    EXPECT_TRUE(!r.trouble().has_value());
    r.seen.second = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    EXPECT_TRUE(r.trouble() == RecordBounce::Trouble::device("\xe2\x80\x9cMic In\xe2\x80\x9d stopped sending audio"));
    shared->guarded([]() {});
    EXPECT_TRUE(!r.trouble().has_value());

    shared->armed.store(false);
    r.seen.second = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    EXPECT_TRUE(!r.trouble().has_value());

    shared->guarded([]() { throw std::runtime_error("a bug in the input"); });
    bool ran = false;
    shared->guarded([&]() { ran = true; });
    EXPECT_TRUE(!ran);
    EXPECT_TRUE(r.trouble() == RecordBounce::Trouble::device("the audio input hit an internal error (see the log)"));

    auto gone = std::make_shared<RecordBounce::SharedCapture>(64);
    gone->fault.store(RecordBounce::SharedCapture::kFaultGone);
    RecordBounce::Recorder rGone(gone, mon, "Mic In", 48000, 1);
    EXPECT_TRUE(rGone.trouble() == RecordBounce::Trouble::device("\xe2\x80\x9cMic In\xe2\x80\x9d is unavailable"));
    return true;
}

// after SoundCraft (crates/playback/src/record.rs takes_stream_into_their_files)
static bool testTakesStreamIntoTheirFiles() {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "polyhymnia-takes-stream";
    fs::remove_all(dir, ec);

    auto [gtr, gtrErr] = RecordBounce::TakeWriter::create(1, {1, 2}, dir / "Gtr_01.wav", 48000);
    EXPECT_TRUE(gtr != nullptr);
    auto [vox, voxErr] = RecordBounce::TakeWriter::create(2, {0}, dir / "Vox_01.wav", 48000);
    EXPECT_TRUE(vox != nullptr);
    auto [dup, dupErr] = RecordBounce::TakeWriter::create(3, {0}, dir / "Vox_01.wav", 48000);
    EXPECT_TRUE(dup == nullptr);

    const std::vector<float> b1(300, 0.5f);
    vox->write(b1.data(), b1.size(), 3);
    vox->writeHeader();
    const std::vector<float> b2(150, 0.25f);
    vox->write(b2.data(), b2.size(), 3);
    vox->file.flush();
    EXPECT_EQ(decodeTake(dir / "Vox_01.wav")[0].size(), size_t(100));

    auto shared = std::make_shared<RecordBounce::SharedCapture>(1 << 16);
    shared->armed.store(true);
    std::vector<std::unique_ptr<RecordBounce::TakeWriter>> writers;
    writers.push_back(std::move(gtr));
    writers.push_back(std::move(vox));
    std::thread writerThread([&]() {
        return RecordBounce::writeTakes(*shared, std::move(writers), 3);
    });

    std::vector<float> input(3 * 4800);
    for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<float>(i) / 20000.0f;
    size_t owed = 0;
    for (size_t off = 0; off < input.size(); off += 3 * 480) {
        shared->capture(input.data() + off, 3 * 480, owed);
    }
    shared->armed.store(false);
    writerThread.join();

    const auto gtrDecoded = decodeTake(dir / "Gtr_01.wav");
    const auto voxDecoded = decodeTake(dir / "Vox_01.wav");
    EXPECT_EQ(gtrDecoded.size(), size_t(2));
    EXPECT_EQ(gtrDecoded[0].size(), size_t(4800));
    EXPECT_EQ(voxDecoded.size(), size_t(1));
    EXPECT_EQ(voxDecoded[0].size(), size_t(100 + 50 + 4800));

    fs::remove_all(dir, ec);
    return true;
}

// after SoundCraft (crates/playback/src/record.rs a_take_sums_itself_up_for_drawing_while_it_records)
static bool testATakeSumsItselfUpForDrawingWhileItRecords() {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "polyhymnia-live-take";
    fs::remove_all(dir, ec);
    auto [take, err] = RecordBounce::TakeWriter::create(4, {2, 0}, dir / "Gtr_01.wav", 48000);
    EXPECT_TRUE(take != nullptr);
    std::vector<float> input;
    for (int f = 0; f < 300; ++f) {
        input.push_back(static_cast<float>(f));
        input.push_back(-1.0f);
        input.push_back(-static_cast<float>(f));
    }
    take->write(input.data(), 3 * 100, 3);
    take->write(input.data() + 3 * 100, 3 * 200, 3);
    take->file.close();
    const RecordBounce::LiveTake live = *take->live;
    const RecordBounce::LiveTake want = {
        {{-127.0f, 0.0f}, {-255.0f, -128.0f}, {-299.0f, -256.0f}},
        {{0.0f, 127.0f}, {128.0f, 255.0f}, {256.0f, 299.0f}}
    };
    EXPECT_EQ(live, want);
    fs::remove_all(dir, ec);
    return true;
}

// after SoundCraft (crates/playback/src/record.rs ring_tests)
static bool testMonitorRingRateConversionAndSilence() {
    // 1. 1 kHz sine at 16 kHz -> 48 kHz
    {
        RecordBounce::InputRing ring(1, 16000);
        std::vector<float> input(4000);
        for (size_t i = 0; i < input.size(); ++i) {
            input[i] = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 1000.0 * static_cast<double>(i) / 16000.0));
        }
        ring.push(input);
        std::vector<std::vector<float>> out = {std::vector<float>(48, 0.0f)};
        size_t k = 0;
        for (int pass = 0; pass < 10; ++pass) {
            ring.popInto(out, 48, 48000);
            for (float v : out[0]) {
                const float want = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 1000.0 * static_cast<double>(k) / 48000.0));
                EXPECT_TRUE(std::fabs(v - want) < 0.05f);
                ++k;
            }
        }
    }
    // 2. Decimation (96 kHz -> 48 kHz)
    {
        RecordBounce::InputRing ring(1, 96000);
        std::vector<float> input(1000);
        for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<float>(i);
        ring.push(input);
        std::vector<std::vector<float>> out = {std::vector<float>(100, 0.0f)};
        ring.popInto(out, 100, 48000);
        for (size_t k = 0; k < 100; ++k) {
            EXPECT_EQ(out[0][k], static_cast<float>(2 * k));
        }
    }
    // 3. Equal rates + NaN/Inf silencing + underrun silence
    {
        RecordBounce::InputRing ring(1, 48000);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const std::vector<float> in = {0.5f, nan, inf, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f};
        ring.push(in);
        std::vector<std::vector<float>> out = {std::vector<float>(4, 1.0f)};
        ring.popInto(out, 4, 48000);
        const std::vector<float> want = {0.5f, 0.0f, 0.0f, -0.5f};
        EXPECT_EQ(out[0], want);

        RecordBounce::InputRing emptyRing(1, 16000);
        std::vector<std::vector<float>> silence = {std::vector<float>(16, 1.0f)};
        emptyRing.popInto(silence, 16, 48000);
        for (float v : silence[0]) EXPECT_EQ(v, 0.0f);
    }
    return true;
}

// after SoundCraft (crates/engine/src/io/record.rs armed_tracks_get_new_take_files_used_in_place,
// a_loop_take_becomes_one_clip_per_pass_and_autosaves_in_place, punching_in_keeps_the_material_around_the_punch_range,
// a_punch_that_misses_the_take_leaves_the_session_alone, a_take_shows_the_pass_under_way_within_the_punch_while_it_records)
static bool testRecordingPlacementLoopAndPunch() {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "polyhymnia-record-tests";
    fs::remove_all(dir, ec);

    // 1. planTakes & addRecording
    {
        EngCmd::Engine e;
        e.doc = Session::empty();
        const uint64_t vox = createArmedTrack(e, "Vox", "Mono", "In 2");
        const uint64_t gtr = createArmedTrack(e, "Gtr", "Stereo", "In 3-4");
        writeTakeFile(dir / "Audio Files/Vox_01.wav", 10);
        const auto plan = RecordBounce::planTakes(e.doc, dir, 4);
        EXPECT_EQ(plan.size(), size_t(2));
        EXPECT_EQ(std::get<0>(plan[0]), vox);
        EXPECT_EQ(std::get<1>(plan[0]), std::vector<size_t>{1});
        EXPECT_EQ(std::get<2>(plan[0]), dir / "Audio Files/Vox_02.wav");
        EXPECT_EQ(std::get<0>(plan[1]), gtr);
        EXPECT_EQ((std::get<1>(plan[1])), (std::vector<size_t>{2, 3}));
        EXPECT_EQ(std::get<2>(plan[1]), dir / "Audio Files/Gtr_01.wav");
        EXPECT_EQ(( std::get<1>(RecordBounce::planTakes(e.doc, dir, 2)[1]) ), (std::vector<size_t>{0, 1}));

        writeTakeFile(std::get<2>(plan[0]), 4800);
        const auto clips = RecordBounce::addRecording(e.doc, 96000, {{vox, std::get<2>(plan[0])}}, std::nullopt);
        EXPECT_EQ(clips.size(), size_t(1));
        const auto fc = Session::findClipByNum(e.doc, clips[0]);
        EXPECT_TRUE(fc.has_value());
        EXPECT_EQ(fc->second->start, 96000LL);
        EXPECT_EQ(fc->second->length, 4800LL);
    }

    // 2. Loop recording into playlists
    {
        const fs::path loopTake = dir / "Audio Files/LoopVox_01.wav";
        writeTakeFile(loopTake, 1050);
        EngCmd::Engine e;
        e.doc = Session::empty();
        const uint64_t vox = createArmedTrack(e, "Vox", "Mono", "In 1");
        const auto clips = RecordBounce::addRecording(e.doc, 1000, {{vox, loopTake}}, 400);
        EXPECT_EQ(clips.size(), size_t(3));
        const Session::Track* t = Session::findTrackByNum(e.doc, vox);
        EXPECT_TRUE(t != nullptr);
        EXPECT_EQ(t->playlists.size(), size_t(3));
        EXPECT_EQ(t->activePlaylist, size_t(2));
        EXPECT_EQ(t->playlists[2].name, std::string("Vox.03"));
        EXPECT_EQ(t->playlists[0].clips[0].offset, 0LL);
        EXPECT_EQ(t->playlists[1].clips[0].offset, 400LL);
        EXPECT_EQ(t->playlists[2].clips[0].offset, 800LL);
        EXPECT_EQ(t->playlists[2].clips[0].length, 250LL);
    }

    // 3. Punching in keeps surrounding material
    {
        EngCmd::Engine e;
        e.doc = Session::empty();
        const uint64_t vox = createArmedTrack(e, "Vox", "Mono", "In 1");
        const fs::path base = dir / "Audio Files/Base.wav";
        const fs::path take = dir / "Audio Files/Take.wav";
        writeTakeFile(base, 3000);
        writeTakeFile(take, 1000);
        RecordBounce::addRecording(e.doc, 0, {{vox, base}}, std::nullopt);
        RecordBounce::Recording plan;
        plan.start = 1000;
        plan.punch = Time::Range{1400, 1600};
        plan.rate = 48000;
        plan.takes = {{vox, take, 1}};
        RecordBounce::placeRecording(e.doc, plan, {{vox, take}});
        const Session::Track* t = Session::findTrackByNum(e.doc, vox);
        EXPECT_TRUE(t != nullptr);
        EXPECT_EQ(t->clips.size(), size_t(3));
        EXPECT_EQ(t->clips[0].start, 0LL);
        EXPECT_EQ(t->clips[0].length, 1400LL);
        EXPECT_EQ(t->clips[1].start, 1400LL);
        EXPECT_EQ(t->clips[1].length, 200LL);
        EXPECT_EQ(t->clips[1].offset, 400LL);
        EXPECT_EQ(t->clips[2].start, 1600LL);
        EXPECT_EQ(t->clips[2].length, 1400LL);
        EXPECT_EQ(t->clips[2].offset, 1600LL);
    }

    // 4. Punch that misses the take leaves session alone
    {
        EngCmd::Engine e;
        e.doc = Session::empty();
        const uint64_t vox = createArmedTrack(e, "Vox", "Mono", "In 1");
        const fs::path take = dir / "Audio Files/MissTake.wav";
        writeTakeFile(take, 1000);
        RecordBounce::Recording plan;
        plan.start = 1000;
        plan.punch = Time::Range{5000, 6000};
        plan.rate = 48000;
        plan.takes = {{vox, take, 1}};
        const auto placed = RecordBounce::placeRecording(e.doc, plan, {{vox, take}});
        EXPECT_TRUE(placed.empty());
        EXPECT_TRUE(e.doc.sources.empty());
    }

    // 5. live_span
    {
        EXPECT_TRUE(!RecordBounce::liveSpan(1000, std::nullopt, std::nullopt, 0).has_value());
        auto s1 = RecordBounce::liveSpan(1000, std::nullopt, std::nullopt, 500);
        EXPECT_TRUE(s1.has_value() && s1->first == 1000 && s1->second == (Time::Range{1000, 1500}));
        auto s2 = RecordBounce::liveSpan(1000, 400, std::nullopt, 900);
        EXPECT_TRUE(s2.has_value() && s2->first == 200 && s2->second == (Time::Range{1000, 1100}));
        EXPECT_TRUE(!RecordBounce::liveSpan(1000, 400, std::nullopt, 800).has_value());
        const Time::Range punch{1200, 1400};
        EXPECT_TRUE(!RecordBounce::liveSpan(1000, std::nullopt, punch, 150).has_value());
        auto s3 = RecordBounce::liveSpan(1000, std::nullopt, punch, 500);
        EXPECT_TRUE(s3.has_value() && s3->first == 1000 && s3->second == (Time::Range{1200, 1400}));
    }

    fs::remove_all(dir, ec);
    return true;
}

// after SoundCraft (crates/dsp/src/meter.rs true_peak_matches_the_shifted_history)
static bool testTruePeakMatchesShiftedHistory() {
    auto noise = [](size_t len, uint64_t seed, float amp) {
        uint64_t x = (seed * 0x9E3779B97F4A7C15ULL) | 1ULL;
        std::vector<float> out(len);
        for (size_t i = 0; i < len; ++i) {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            out[i] = (static_cast<float>(x >> 40) / static_cast<float>(1ULL << 24) * 2.0f - 1.0f) * amp;
        }
        return out;
    };
    auto tpRef = [](const auto& coefs, const std::vector<float>& x) {
        std::array<float, RecordBounce::LoudnessMeter::kTpTaps> h{};
        float tp = 0.0f;
        for (float raw : x) {
            const float v = std::isfinite(raw) ? raw : 0.0f;
            for (size_t k = RecordBounce::LoudnessMeter::kTpTaps - 1; k > 0; --k) h[k] = h[k - 1];
            h[0] = v;
            float peak = std::fabs(v);
            for (size_t p = 0; p < RecordBounce::LoudnessMeter::kTpPhases; ++p) {
                float y = 0.0f;
                for (size_t t = 0; t < RecordBounce::LoudnessMeter::kTpTaps; ++t) y += coefs[t][p] * h[t];
                peak = std::max(peak, std::fabs(y));
            }
            if (peak > tp) tp = peak;
        }
        return tp;
    };

    const size_t len = 4000;
    std::vector<float> l = noise(len, 1, 0.3f);
    for (size_t i = 0; i < len; ++i) l[i] += 0.6f * std::sin(6.2831853f * 0.25f * static_cast<float>(i) + 0.7f);
    std::vector<float> r = noise(len, 2, 0.9f);
    l[123] = std::numeric_limits<float>::quiet_NaN();
    l[2345] = std::numeric_limits<float>::infinity();

    for (size_t block : {size_t(1), size_t(7), size_t(480), len}) {
        RecordBounce::LoudnessMeter m(48000.0f, 2);
        const float want = std::max(tpRef(m.tpCoefs(), l), tpRef(m.tpCoefs(), r));
        for (size_t at = 0; at < len; at += block) {
            const size_t n = std::min(block, len - at);
            std::vector<std::vector<float>> chunk = {
                std::vector<float>(l.begin() + static_cast<std::ptrdiff_t>(at), l.begin() + static_cast<std::ptrdiff_t>(at + n)),
                std::vector<float>(r.begin() + static_cast<std::ptrdiff_t>(at), r.begin() + static_cast<std::ptrdiff_t>(at + n))
            };
            m.process(chunk, n);
        }
        EXPECT_TRUE(std::fabs(m.truePeakLinear() - want) < 1e-5f);
    }
    return true;
}

// after SoundCraft (crates/engine/src/cmd/file/tests.rs bounce_stems_rejects_unsupported_formats_before_writing,
// bounce_stems_accepts_explicit_wav_format, bounce_refuses_formats_it_cannot_write, bounce_source_renders_the_named_bus,
// same_named_clips_export_as_separate_files)
static bool testBounceMixStemsFormatsAndExportClips() {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "polyhymnia-bounce-tests";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // Prepare a 4800-sample tone WAV
    Session::AudioBuffer toneBuf;
    toneBuf.sampleRate = 48000;
    toneBuf.channels.assign(1, std::vector<float>(4800, 0.0f));
    for (size_t i = 0; i < 4800; ++i) toneBuf.channels[0][i] = std::sin(static_cast<float>(i) * 0.1f) * 0.25f;
    const auto toneBytes = ImportAudio::encodeWav(toneBuf, 24);
    const fs::path tonePath = dir / "tone.wav";
    {
        std::ofstream f(tonePath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(toneBytes.data()), static_cast<std::streamsize>(toneBytes.size()));
    }

    // 1. Unsupported formats refused; WAV, AIFF, FLAC, MP3 all succeed and round-trip decode!
    {
        EngCmd::Engine e;
        e.doc = Session::empty();
        EngCmd::Engine::Params pImp;
        pImp.strs["path"] = tonePath.u8string();
        e.execute("file.import_audio", pImp);

        for (const char* badName : {"mix.ogg", "mix.xyz"}) {
            const fs::path p = dir / badName;
            EngCmd::Engine::Params pB;
            pB.strs["path"] = p.u8string();
            pB.nums["start"] = 0;
            pB.nums["end"] = 480;
            const auto r = e.execute("file.bounce_mix", pB);
            EXPECT_TRUE(!r.ok);
            EXPECT_TRUE(!fs::exists(p, ec));
        }

        for (const char* goodName : {"mix.wav", "mix.aiff", "mix.flac", "mix.mp3"}) {
            const fs::path p = dir / goodName;
            EngCmd::Engine::Params pB;
            pB.strs["path"] = p.u8string();
            pB.nums["start"] = 0;
            pB.nums["end"] = 1920;
            const auto r = e.execute("file.bounce_mix", pB);
            EXPECT_TRUE(r.ok);
            EXPECT_TRUE(fs::exists(p, ec));
            const auto chs = decodeTake(p);
            EXPECT_TRUE(!chs.empty() && !chs[0].empty());
        }
    }

    // 2. bounce_source_renders_the_named_bus
    {
        EngCmd::Engine e;
        e.doc = Session::empty();
        EngCmd::Engine::Params pImp;
        pImp.strs["path"] = tonePath.u8string();
        e.execute("file.import_audio", pImp);
        const uint64_t stemBus = Session::addBus(e.doc, "Stem", Session::ChannelFormat::Stereo);
        Session::addBus(e.doc, "Empty", Session::ChannelFormat::Stereo);
        e.doc.tracks[0].mixer.sends[0] = Session::SendSlot::make(Session::Route::bus(stemBus), -12.0f);

        auto runBounce = [&](const char* fname, std::optional<std::string> src) {
            EngCmd::Engine::Params p;
            p.strs["path"] = (dir / fname).u8string();
            p.nums["start"] = 0;
            p.nums["end"] = 4800;
            p.strs["bit_depth"] = "32f";
            p.bools["dither"] = false;
            if (src.has_value()) p.strs["source"] = *src;
            return e.execute("file.bounce_mix", p);
        };
        const auto rMain = runBounce("main.wav", std::nullopt);
        const auto rNamedMain = runBounce("named-main.wav", "main");
        const auto rStem = runBounce("stem.wav", "STEM");
        const auto rEmpty = runBounce("empty.wav", "Empty");
        EXPECT_TRUE(rMain.ok && rNamedMain.ok && rStem.ok && rEmpty.ok);
        const double mainPeak = rMain.nums.at("peak_db");
        const double stemPeak = rStem.nums.at("peak_db");
        const double emptyPeak = rEmpty.nums.at("peak_db");
        EXPECT_TRUE(std::fabs(mainPeak - rNamedMain.nums.at("peak_db")) < 1e-4);
        EXPECT_TRUE(mainPeak > -20.0);
        EXPECT_TRUE(mainPeak - stemPeak > 6.0 && mainPeak - stemPeak < 18.0);
        EXPECT_TRUE(emptyPeak < -100.0);

        const auto rMissing = runBounce("missing.wav", "NoSuchBus");
        EXPECT_TRUE(!rMissing.ok);
        EXPECT_TRUE(!fs::exists(dir / "missing.wav", ec));
    }

    // 3. bounce_stems & export_clips
    {
        EngCmd::Engine e;
        e.doc = Session::empty();
        EngCmd::Engine::Params pImp;
        pImp.strs["path"] = tonePath.u8string();
        e.execute("file.import_audio", pImp);

        const fs::path badStems = dir / "bad_stems";
        EngCmd::Engine::Params pBad;
        pBad.strs["dir"] = badStems.u8string();
        pBad.strs["format"] = "not-a-format";
        pBad.nums["start"] = 0;
        pBad.nums["end"] = 32;
        EXPECT_TRUE(!e.execute("file.bounce_stems", pBad).ok);
        EXPECT_TRUE(!fs::exists(badStems, ec));

        const fs::path goodStems = dir / "good_stems";
        EngCmd::Engine::Params pGood;
        pGood.strs["dir"] = goodStems.u8string();
        pGood.strs["format"] = "wav";
        pGood.nums["start"] = 0;
        pGood.nums["end"] = 32;
        pGood.bools["dither"] = false;
        const auto rStems = e.execute("file.bounce_stems", pGood);
        EXPECT_TRUE(rStems.ok);
        EXPECT_EQ(rStems.nums.at("files"), 1.0);

        // Duplicate clip export -> Take.wav, Take-2.wav
        const uint64_t c1 = e.doc.tracks[0].clips[0].numId;
        const std::string srcId = e.doc.tracks[0].clips[0].sourceId;
        const uint64_t c2 = Session::newClipNumId(e.doc);
        Session::Clip second;
        second.id = "c" + std::to_string(c2);
        second.numId = c2;
        second.name = "Take";
        second.sourceId = srcId;
        second.start = 10000;
        second.length = 4800;
        e.doc.tracks[0].clips[0].name = "Take";
        EngEdit::placeClip(e.doc, e.doc.tracks[0].id, second);

        const fs::path expDir = dir / "exported_clips";
        EngCmd::Engine::Params pExp;
        pExp.strs["dir"] = expDir.u8string();
        pExp.idLists["clips"] = {c1, c2};
        const auto rExp = e.execute("file.export_clips", pExp);
        EXPECT_TRUE(rExp.ok);
        EXPECT_EQ(rExp.nums.at("written"), 2.0);
        EXPECT_TRUE(fs::exists(expDir / "Take.wav", ec));
        EXPECT_TRUE(fs::exists(expDir / "Take-2.wav", ec));
    }

    fs::remove_all(dir, ec);
    return true;
}

} // namespace

void registerRecordBounceTests() {
    Test::add("record_a_full_ring_owes_the_lost_samples_as_silence", testFullRingOwesLostSamplesAsSilence);
    Test::add("record_nan_and_infinite_input_records_as_silence", testNanAndInfiniteInputRecordsAsSilence);
    Test::add("record_a_bug_a_stall_or_a_lost_device_is_trouble", testABugAStallOrALostDeviceIsTrouble);
    Test::add("record_takes_stream_into_their_files", testTakesStreamIntoTheirFiles);
    Test::add("record_a_take_sums_itself_up_for_drawing_while_it_records", testATakeSumsItselfUpForDrawingWhileItRecords);
    Test::add("record_monitor_ring_rate_conversion_and_silence", testMonitorRingRateConversionAndSilence);
    Test::add("record_placement_loop_and_punch", testRecordingPlacementLoopAndPunch);
    Test::add("meter_true_peak_matches_the_shifted_history", testTruePeakMatchesShiftedHistory);
    Test::add("bounce_mix_stems_formats_and_export_clips", testBounceMixStemsFormatsAndExportClips);
}
