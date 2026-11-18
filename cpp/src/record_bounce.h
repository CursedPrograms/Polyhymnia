// Polyhymnia - Audio recording, input monitoring, punch in/out, loudness metering, and offline bounce/export
// (after SoundCraft crates/playback/src/record.rs, crates/engine/src/io/record.rs, crates/engine/src/io.rs,
// crates/dsp/src/meter.rs, crates/ui-egui/src/bounce.rs, crates/ui-egui/src/dialogs.rs).
#pragma once

#include "dsp.h"
#include "engine.h"
#include "import_audio.h"
#include "session.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace RecordBounce {

// ─── Recording & Input Monitoring (after crates/playback/src/record.rs) ─────────────────────────

constexpr size_t kLiveBlock = 128;
constexpr size_t kLiveBlocks = size_t(1) << 22;
constexpr size_t kRingSeconds = 4;

using LiveTake = std::vector<std::vector<std::pair<float, float>>>;
using Takes = std::pair<std::vector<std::pair<uint64_t, std::filesystem::path>>, std::optional<std::string>>;

// after SoundCraft (crates/playback/src/record.rs Trouble)
struct Trouble {
    enum class Kind { Device, Disk };
    Kind kind = Kind::Device;
    std::string message;

    static Trouble device(std::string m) { return {Kind::Device, std::move(m)}; }
    static Trouble disk(std::string m) { return {Kind::Disk, std::move(m)}; }
    bool operator==(const Trouble& o) const { return kind == o.kind && message == o.message; }
    bool operator!=(const Trouble& o) const { return !(*this == o); }
};

// after SoundCraft (crates/playback/src/record.rs CaptureRing)
class CaptureRing {
public:
    explicit CaptureRing(size_t capacity);

    size_t push(size_t n, const std::function<float(size_t)>& sample);
    void popAll(std::vector<float>& out);
    void clear();

private:
    std::vector<std::atomic<uint32_t>> slots_;
    size_t mask_ = 0;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

// after SoundCraft (crates/playback/src/record.rs Shared)
struct SharedCapture {
    static constexpr uint8_t kFaultNone = 0;
    static constexpr uint8_t kFaultGone = 1;
    static constexpr uint8_t kFaultBroken = 2;

    std::atomic<bool> armed{false};
    CaptureRing ring;
    std::atomic<size_t> dropped{0};
    std::atomic<uint64_t> beats{0};
    std::atomic<uint8_t> fault{kFaultNone};
    std::mutex failedMtx;
    std::optional<std::string> failed;

    explicit SharedCapture(size_t ringCapacity);

    void guarded(const std::function<void()>& work);
    void capture(const float* data, size_t len, size_t& owed);
};

// after SoundCraft (crates/playback/src/record.rs InputRing)
class InputRing {
public:
    InputRing(size_t channels, uint32_t rate);

    void push(const float* samples, size_t len);
    void push(const std::vector<float>& samples) { push(samples.data(), samples.size()); }
    void popInto(std::vector<std::vector<float>>& out, size_t frames, uint32_t sessionRate);

    std::atomic<size_t> channels;
    uint32_t rate() const { return rate_; }

private:
    struct RingState {
        std::deque<float> data;
        double phase = 0.0;
    };
    std::mutex mtx_;
    RingState state_;
    uint32_t rate_ = 48000;
    size_t cap_ = 24000;
};

// after SoundCraft (craft-audio wav_float_header)
std::vector<uint8_t> wavFloatHeader(uint32_t sampleRate, uint16_t channels, uint64_t frames);

// after SoundCraft (crates/playback/src/record.rs TakeWriter)
class TakeWriter {
public:
    uint64_t trackId = 0;
    std::vector<size_t> inputs;
    std::filesystem::path path;
    std::fstream file;
    uint32_t rate = 48000;
    uint64_t frames = 0;
    std::optional<std::string> error;
    std::shared_ptr<std::mutex> liveMtx;
    std::shared_ptr<LiveTake> live;

    static std::pair<std::unique_ptr<TakeWriter>, std::string>
    create(uint64_t id, std::vector<size_t> inputs, std::filesystem::path path, uint32_t rate);

    void write(const float* block, size_t blockSamples, size_t channels);
    void summarize(const float* block, size_t blockSamples, size_t channels);
    void writeHeader();
    void fail(const std::string& msg);
};

Takes writeTakes(SharedCapture& shared, std::vector<std::unique_ptr<TakeWriter>> takes, size_t channels);

// after SoundCraft (crates/playback/src/record.rs Recorder)
class Recorder {
public:
    std::shared_ptr<SharedCapture> shared;
    std::shared_ptr<InputRing> monitor;
    std::string deviceName = "Default Input";
    uint32_t sampleRate = 48000;
    size_t channels = 2;
    std::pair<uint64_t, std::chrono::steady_clock::time_point> seen;

    Recorder(std::shared_ptr<SharedCapture> sh, std::shared_ptr<InputRing> mon, std::string name, uint32_t sr, size_t ch);
    ~Recorder();

    static std::pair<std::unique_ptr<Recorder>, std::string> open(uint32_t preferredRate = 48000, size_t preferredChannels = 2);

    std::string arm(const std::vector<std::tuple<uint64_t, std::vector<size_t>, std::filesystem::path>>& takes);
    std::optional<LiveTake> liveTake(uint64_t trackId) const;
    std::optional<Trouble> trouble();
    uint64_t lostMs() const;
    Takes finish();

private:
    struct DeviceImpl;
    std::unique_ptr<DeviceImpl> dev_;
    std::optional<std::thread> writerThread_;
    Takes writerResult_;
    std::mutex resultMtx_;
    std::vector<std::tuple<uint64_t, std::shared_ptr<std::mutex>, std::shared_ptr<LiveTake>>> liveTakes_;
};

// ─── Engine Recording Plan & Placement (after crates/engine/src/io/record.rs, io.rs) ────────────

// after SoundCraft (crates/engine/src/io/record.rs Take)
struct Take {
    uint64_t track = 0;
    std::filesystem::path path;
    size_t channels = 1;
    bool operator==(const Take& o) const { return track == o.track && path == o.path && channels == o.channels; }
};

// after SoundCraft (crates/engine/src/io/record.rs Recording)
struct Recording {
    long long start = 0;
    std::optional<long long> pass;
    std::optional<Time::Range> punch;
    uint32_t rate = 48000;
    std::vector<Take> takes;
};

// after SoundCraft (crates/engine/src/io.rs plan_takes)
std::vector<std::tuple<uint64_t, std::vector<size_t>, std::filesystem::path>>
planTakes(const Session::Document& doc, const std::filesystem::path& unsavedDir, size_t inputChannels);

// after SoundCraft (crates/engine/src/io/record.rs live_span)
std::optional<std::pair<long long, Time::Range>>
liveSpan(long long start, std::optional<long long> pass, std::optional<Time::Range> punch, long long len);

// after SoundCraft (crates/engine/src/io/record.rs add_recording, place_recording)
std::vector<uint64_t> addRecording(Session::Document& doc, long long start,
                                   const std::vector<std::pair<uint64_t, std::filesystem::path>>& takes,
                                   std::optional<long long> pass = std::nullopt);
std::vector<uint64_t> placeRecording(Session::Document& doc, const Recording& plan,
                                     const std::vector<std::pair<uint64_t, std::filesystem::path>>& takes);

// ─── Level & Loudness Meters (after crates/dsp/src/meter.rs) ────────────────────────────────────

// after SoundCraft (crates/dsp/src/meter.rs PeakMeter)
class PeakMeter {
public:
    explicit PeakMeter(float sampleRate = 48000.0f);
    void setReleaseDbPerSec(float rate);
    void setHoldSecs(float secs);
    void process(const float* samples, size_t count);
    void feed(float peak, size_t frames);
    float levelDb() const { return levelDb_; }
    float peakHoldDb() const { return holdDb_; }
    bool clipped() const { return clipped_; }
    void resetClip() { clipped_ = false; }
    void reset();

private:
    float sr_ = 48000.0f;
    float levelDb_ = Dsp::kMinDb;
    float holdDb_ = Dsp::kMinDb;
    float holdLeft_ = 0.0f;
    float holdSecs_ = 2.0f;
    float releaseDbPerSec_ = 20.0f;
    bool clipped_ = false;
};

// after SoundCraft (crates/dsp/src/meter.rs RmsMeter)
class RmsMeter {
public:
    explicit RmsMeter(float sampleRate = 48000.0f);
    void setWindowMs(float ms);
    void process(const float* samples, size_t count);
    float rmsDb() const;
    void reset() { ms_ = 0.0; }

private:
    float sr_ = 48000.0f;
    float coef_ = 0.0f;
    double ms_ = 0.0;
};

// after SoundCraft (crates/dsp/src/meter.rs LoudnessMeter)
class LoudnessMeter {
public:
    static constexpr size_t kSubBlocks = 30;
    static constexpr float kHistMin = -70.0f;
    static constexpr float kHistStep = 0.05f;
    static constexpr size_t kHistBins = 1800;
    static constexpr size_t kTpPhases = 4;
    static constexpr size_t kTpTaps = 12;

    LoudnessMeter(float sampleRate = 48000.0f, size_t channels = 2);

    void setChannelWeight(size_t channel, float weight);
    float sampleRate() const { return sr_; }
    void reset();
    void process(const std::vector<std::vector<float>>& io, size_t frames);

    float momentaryLufs() const;
    float shortTermLufs() const;
    float maxMomentaryLufs() const { return maxMomentary_; }
    float integratedLufs() const;
    float truePeakDb() const { return Dsp::gainToDb(truePeak_); }
    float truePeakLinear() const { return truePeak_; }
    const std::array<std::array<float, kTpPhases>, kTpTaps>& tpCoefs() const { return tpCoefs_; }

private:
    float sr_ = 48000.0f;
    std::vector<float> weights_;
    std::vector<std::array<Dsp::Biquad, 2>> filters_;
    size_t blockLen_ = 4800;
    size_t blockPos_ = 0;
    std::vector<double> blockAcc_;
    std::array<double, kSubBlocks> sub_{};
    size_t subIdx_ = 0;
    size_t subFilled_ = 0;
    std::vector<uint64_t> histCount_;
    std::vector<double> histEnergy_;
    std::array<std::array<float, kTpPhases>, kTpTaps> tpCoefs_{};
    std::vector<std::array<float, 2 * kTpTaps>> tpHist_;
    std::vector<size_t> tpPos_;
    float truePeak_ = 0.0f;
    float maxMomentary_ = Dsp::kMinDb;

    void truePeakSample(size_t c, float x);
    void finishSubBlock();
    double recentEnergy(size_t n) const;
};

// ─── Bounce & Export (after crates/engine/src/io.rs, crates/ui-egui/src/bounce.rs) ──────────────

enum class EncodeFormat {
    Wav,
    Aiff,
    Flac,
    Mp3
};

enum class BitDepth {
    Int16,
    Int24,
    Float32
};

struct EncodeOptions {
    EncodeFormat format = EncodeFormat::Wav;
    BitDepth bitDepth = BitDepth::Int24;
    bool dither = true;
    std::optional<uint64_t> bwfTimeRef;
};

const char* encodeFormatExtension(EncodeFormat fmt);
const char* encodeFormatLabel(EncodeFormat fmt);
std::optional<EncodeFormat> encodeFormatFor(const std::string& extOrName);
std::filesystem::path bouncePath(const std::string& path, EncodeFormat format);

std::vector<uint8_t> encodeMp3(const Session::AudioBuffer& buf);
std::vector<uint8_t> encodeAudio(const Session::AudioBuffer& buf, const EncodeOptions& opts);

// after SoundCraft (crates/engine/src/io.rs Bounced)
struct Bounced {
    uint64_t bytes = 0;
    float peakDb = Dsp::kMinDb;
    float lufs = Dsp::kMinDb;
    float truePeakDb = Dsp::kMinDb;
};

// after SoundCraft (crates/engine/src/io.rs Bounce)
class Bounce {
public:
    static std::pair<std::unique_ptr<Bounce>, std::string>
    create(const Session::Document& doc, const std::filesystem::path& path, Time::Range range,
           const EncodeOptions& opts, std::optional<uint64_t> busId = std::nullopt,
           bool normalize = false, bool foldStereo = false);

    ~Bounce();

    const std::filesystem::path& path() const { return path_; }
    Time::Range range() const { return range_; }
    float progress() const;

    std::pair<std::optional<Bounced>, std::string> step(const std::function<bool()>& more);
    std::pair<std::optional<Bounced>, std::string> run();

private:
    Bounce(Session::Document doc, std::filesystem::path path, std::filesystem::path part,
           Time::Range range, EncodeOptions opts, std::optional<uint64_t> busId,
           bool normalize, bool foldStereo);

    Session::Document doc_;
    std::filesystem::path path_;
    std::filesystem::path part_;
    Time::Range range_{0, 0};
    EncodeOptions opts_;
    std::optional<uint64_t> busId_;
    bool normalize_ = false;
    bool foldStereo_ = false;
    bool measuring_ = false;
    long long pos_ = 0;
    size_t done_ = 0;
    float gain_ = 1.0f;
    float peak_ = 0.0f;
    LoudnessMeter meter_;
    Session::AudioBuffer accumulated_;
    bool completed_ = false;

    void renderBlock(long long startSample, size_t frames, std::vector<std::vector<float>>& outPlanar) const;
    std::pair<Bounced, std::string> finish();
};

// after SoundCraft (crates/engine/src/io.rs bounce_stems_with, export_clips)
std::pair<std::vector<std::string>, std::string>
bounceStems(const Session::Document& doc, const std::string& dir, Time::Range range,
            const EncodeOptions& opts, bool foldStereo = false);

std::pair<size_t, std::string>
exportClips(const Session::Document& doc, const std::vector<uint64_t>& clipIds, const std::string& dir);

// ─── UI State for Recording & Bounce Dialog (after crates/ui-egui/src/bounce.rs, dialogs.rs) ────

void openBounceDialog(const Session::Document& doc);
void closeBounceDialog();
bool isBounceDialogOpen();
bool isBounceRunning();
void stepBounceUi(std::string& statusOut);
void drawBounceUi(Session::Document& doc, std::string& statusOut);

// Global recording transport hooks used by App / Toolbar / Edit window.
bool isRecording();
long long recordStartSample();
std::optional<Time::Range> recordPunchRange();
std::optional<LiveTake> currentLiveTake(uint64_t trackNumId);
bool toggleRecording(Session::Document& doc, std::string& statusOut);
void stopRecording(Session::Document& doc, std::string& statusOut);
void tickRecording(Session::Document& doc, std::string& statusOut);

} // namespace RecordBounce
