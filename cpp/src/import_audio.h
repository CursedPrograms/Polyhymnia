// Polyhymnia - audio import, format probing/decoding, and background import worker
// (after SoundCraft crates/model/src/source.rs, crates/engine/src/io/import.rs, crates/ui-egui/src/import.rs).
#pragma once

#include "engine.h"
#include "session.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ImportAudio {

// after SoundCraft (craft-audio Container)
enum class Container {
    Wav,
    Aiff,
    Flac,
    MpegAudio,
    Ogg,
    Caf,
    Adts,
    Mp4,
    QuickTime,
    Other
};

// after SoundCraft (craft-audio CodecId)
enum class CodecId {
    Pcm,
    Flac,
    Mp3,
    Vorbis,
    Opus,
    Aac,
    HeAac,
    Alac,
    Ac3,
    Other
};

struct BwfInfo {
    uint64_t timeReference = 0;
};

// after SoundCraft (craft-audio AudioInfo)
struct AudioInfo {
    Container container = Container::Wav;
    CodecId codec = CodecId::Pcm;
    uint32_t sampleRate = 48000;
    uint16_t channels = 1;
    uint64_t frames = 0;
    std::optional<BwfInfo> bwf;
};

// after SoundCraft (crates/model/src/source.rs FileFormat::of)
Session::FileFormat fileFormatOf(const AudioInfo& info);
std::optional<Container> containerFromName(const std::string& name);
std::optional<CodecId> codecFromName(const std::string& name);

// Decode/probe/encode helpers for WAV, AIFF, FLAC, MP3, OGG.
bool probeBytes(const uint8_t* data, size_t len, std::optional<std::string> extHint, AudioInfo& outInfo, std::string& outErr);
bool decodeBytes(const uint8_t* data, size_t len, std::optional<std::string> extHint, AudioInfo& outInfo,
                 Session::AudioBuffer& outBuf, std::string& outErr);
std::vector<uint8_t> encodeWav(const Session::AudioBuffer& buf, int bitDepth = 24, std::optional<uint64_t> bwfTimeRef = std::nullopt);
std::vector<uint8_t> encodeAiff(const Session::AudioBuffer& buf, int bitDepth = 16);
std::vector<uint8_t> encodeFlac(const Session::AudioBuffer& buf);

// after SoundCraft (crates/engine/src/io/import.rs MAX_IMPORT_SAMPLES, MAX_RATE_EXPANSION)
constexpr size_t kMaxImportSamples = size_t(1) << 30;
constexpr uint32_t kMaxRateExpansion = 128;

// after SoundCraft (crates/engine/src/io/import.rs RateError)
struct RateError {
    enum class Kind { TooLarge, Ratio };
    Kind kind = Kind::TooLarge;
    uint32_t from = 0;
    uint32_t to = 0;

    static RateError tooLarge() { return {Kind::TooLarge, 0, 0}; }
    static RateError ratio(uint32_t f, uint32_t t) { return {Kind::Ratio, f, t}; }
    bool operator==(const RateError& o) const { return kind == o.kind && from == o.from && to == o.to; }
    std::string message(const std::string& sizeMsg) const;
};

// after SoundCraft (crates/engine/src/io/import.rs at_rate)
std::pair<std::optional<Session::AudioBuffer>, std::optional<RateError>>
atRate(Session::AudioBuffer buf, uint32_t rate, size_t limit = kMaxImportSamples);

// after SoundCraft (crates/engine/src/io/import.rs unique_audio_rel)
std::string uniqueAudioRel(const Session::Document& s, const std::string& stem);

// after SoundCraft (crates/engine/src/io/import.rs add_source / add_source_audio)
uint64_t addSource(Session::Document& s, const std::string& name, Session::AudioBuffer buf,
                   std::optional<std::string> path, Session::FileFormat format);
uint64_t addSourceAudio(Session::Document& s, const std::string& name, std::shared_ptr<Session::SourceAudio> audio,
                        std::optional<std::string> path, Session::FileFormat format);

// after SoundCraft (crates/engine/src/io/import.rs stamp_at_rate)
uint64_t stampAtRate(uint64_t stamp, uint32_t fileRate, uint32_t sessionRate);

// after SoundCraft (crates/engine/src/io/import.rs DecodedImport)
struct DecodedImport {
    std::filesystem::path key;
    uint32_t rate = 48000;
    AudioInfo info;
    std::shared_ptr<Session::SourceAudio> audio;

    size_t frames() const { return audio ? audio->frames() : 0; }
};

std::filesystem::path fileKey(const std::filesystem::path& p);

// after SoundCraft (crates/engine/src/io/import.rs decode_import / decode_import_bytes)
std::pair<std::optional<DecodedImport>, std::string> decodeImport(const std::string& path, uint32_t rate);
std::pair<std::optional<std::pair<AudioInfo, std::shared_ptr<Session::SourceAudio>>>, std::string>
decodeImportBytes(const std::string& name, const uint8_t* bytes, size_t len, uint32_t rate);

// after SoundCraft (crates/engine/src/io/import.rs import_audio_bytes / place_decoded / place_import)
EngCmd::Result importAudioBytes(Session::Document& s, const std::string& name, const uint8_t* bytes, size_t len,
                                std::optional<std::string> path, std::optional<uint64_t> target, long long at,
                                bool clipsListOnly = false);
EngCmd::Result placeDecoded(Session::Document& s, const std::string& name, const DecodedImport& d,
                            std::optional<std::string> path, std::optional<uint64_t> target, long long at,
                            bool clipsListOnly = false);
EngCmd::Result placeImport(Session::Document& s, const std::string& name, const AudioInfo& info,
                           std::shared_ptr<Session::SourceAudio> audio, std::optional<std::string> path,
                           std::optional<uint64_t> target, long long at, bool clipsListOnly = false);

// Copy an external audio file into `<doc.folder>/Audio Files/` and return the relative path ("Audio Files/...").
std::string copyToProjectAudioFiles(const Session::Document& doc, const std::string& sourcePath);

// Supported audio extensions for dialogs and drag-drop.
bool isSupportedAudioPath(const std::string& path);

// after SoundCraft (crates/ui-egui/src/import.rs Imports)
class Imports {
public:
    bool busy() const;
    std::optional<std::pair<std::string, size_t>> label() const;

    void start(EngCmd::Engine& engine, std::string& statusOut, const EngCmd::Engine::Params& params);
    void step(EngCmd::Engine& engine, std::string& statusOut);
    void show() const;

private:
    struct Running {
        EngCmd::Engine::Params params;
        std::string name;
        uint32_t rate = 48000;
        std::future<std::pair<std::optional<DecodedImport>, std::string>> rx;
    };

    std::deque<EngCmd::Engine::Params> queue_;
    std::optional<Running> running_;

    void spawn(EngCmd::Engine& engine, std::string& statusOut, EngCmd::Engine::Params params);
    void finish(EngCmd::Engine& engine, std::string& statusOut);
    bool runNow(EngCmd::Engine& engine, std::string& statusOut, const EngCmd::Engine::Params& params);
};

} // namespace ImportAudio
