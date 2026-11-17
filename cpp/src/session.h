// Polyhymnia - session document: tracks, clips, markers, sources (after SoundCraft crates/model).
#pragma once
#include "tempo.h"
#include "time_grid.h"
#include "time_range.h"
#include "timecode.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Session {

// after SoundCraft (crates/model/src/source.rs FileFormat)
enum class FileFormat { Wav, Aiff, Flac, Mp3, Ogg, Aac, Alac, Caf, Other };

const char* fileFormatName(FileFormat f);
std::optional<FileFormat> fileFormatFromName(const std::string& name);

// after SoundCraft (crates/model/src/mixer.rs ChannelFormat)
enum class ChannelFormat {
    Mono,
    Stereo,
    Lcr,
    Quad,
    Lcrs,
    Surround50,
    Surround51,
    Surround60,
    Surround61,
    Sdds70,
    Sdds71,
    Surround70,
    Surround71,
    Atmos702,
    Atmos712,
    Atmos502,
    Atmos512,
    Atmos504,
    Atmos514,
    Atmos704,
    Atmos714,
    Atmos706,
    Atmos716,
    Atmos904,
    Atmos914,
    Atmos906,
    Atmos916,
    Ambisonics1,
    Ambisonics2,
    Ambisonics3,
    Ambisonics4,
    Ambisonics5,
    Ambisonics6,
    Ambisonics7
};

size_t channelFormatChannels(ChannelFormat f);
const char* channelFormatLabel(ChannelFormat f);
std::optional<ChannelFormat> channelFormatFromId(const std::string& s);
ChannelFormat channelFormatForChannels(size_t n);

// after SoundCraft (crates/model/src/track.rs TRACK_COLORS)
extern const std::array<std::array<uint8_t, 3>, 16> kTrackColors;

enum class FadeShape { Linear, EqualPower, SCurve, Exponential, Logarithmic };

struct Fade {
    long long len = 0;
    FadeShape shape = FadeShape::EqualPower;
    float curvature = 0.0f;
};

enum class TrackKind { Audio, Aux, Master, Midi, Instrument, Vca, Folder, Video };

// after SoundCraft (crates/model/src/track.rs TrackKind::has_playlist)
inline bool hasPlaylist(TrackKind k) {
    return k == TrackKind::Audio || k == TrackKind::Midi || k == TrackKind::Instrument || k == TrackKind::Video;
}

// after SoundCraft (crates/model/src/track.rs TrackKind::is_midi)
inline bool isMidiKind(TrackKind k) { return k == TrackKind::Midi || k == TrackKind::Instrument; }

const char* trackKindDefaultName(TrackKind k);
const char* trackKindLabel(TrackKind k);
const char* trackKindId(TrackKind k);
std::optional<TrackKind> trackKindFromId(const std::string& s);

struct MidiNote {
    uint8_t pitch = 60;
    uint8_t velocity = 100;
    uint8_t releaseVelocity = 64;
    uint8_t channel = 0;
    long long start = 0;
    long long length = 0;
    bool operator==(const MidiNote& o) const {
        return pitch == o.pitch && velocity == o.velocity && releaseVelocity == o.releaseVelocity && channel == o.channel &&
               start == o.start && length == o.length;
    }
};

struct MidiCtrl {
    long long tick = 0;
    uint8_t channel = 0;
    int kind = 0;
    int value = 0;
};

struct MidiSequence {
    std::vector<MidiNote> notes;
    std::vector<MidiCtrl> ctrls;
};

struct Clip {
    std::string id;
    uint64_t numId = 0;
    std::string name;
    std::string sourceId;
    long long start = 0;
    long long offset = 0;
    long long length = 0;
    float gainDb = 0.0f;
    std::vector<std::pair<long long, float>> gainEnv;
    Fade fadeIn;
    Fade fadeOut;
    bool muted = false;
    bool editLocked = false;
    bool timeLocked = false;
    long long syncPoint = 0;
    double stretch = 1.0;
    bool isMidi = false;
    std::optional<MidiSequence> midi;
    std::optional<uint64_t> loopId;
    std::optional<uint64_t> group;
    std::optional<uint64_t> autoNameScheme;
    std::optional<std::array<uint8_t, 3>> color;
    long long end() const;
};

struct Playlist {
    std::string name;
    std::vector<Clip> clips;
};

struct AutomationPoint {
    long long at = 0;
    float value = 0.0f;
};

enum class AutoParamKind { Volume, Pan, Mute, SendLevel };

struct AutoParam {
    AutoParamKind kind = AutoParamKind::Volume;
    uint8_t index = 0;
    bool operator<(const AutoParam& o) const;
    bool operator==(const AutoParam& o) const { return kind == o.kind && index == o.index; }
};

struct AutomationLane {
    AutoParam param;
    std::vector<AutomationPoint> points;
    bool enabled = true;
    void setPoint(long long at, float value);
    void clearRange(long long start, long long end);
    float valueAt(long long at, float fallback) const;
};

struct Track {
    std::string id;
    uint64_t numId = 0;
    std::string name;
    TrackKind kind = TrackKind::Audio;
    ChannelFormat format = ChannelFormat::Mono;
    std::array<uint8_t, 3> color = {86, 132, 214};
    bool mute = false;
    bool solo = false;
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool hidden = false;
    bool elastic = false;
    std::string folderParent;
    bool folderOpen = true;
    bool isFolder = false;
    std::vector<Clip> clips;
    std::vector<Playlist> playlists;
    size_t activePlaylist = 0;
    std::optional<size_t> targetPlaylist;
    std::string view;
    std::vector<AutomationLane> automation;
    AutomationLane* laneMut(const AutoParam& p);
    const AutomationLane* lane(const AutoParam& p) const;
    std::vector<Clip>& activeClips();
    const std::vector<Clip>& activeClips() const;
};

struct Marker {
    std::string id;
    std::string name;
    long long start = 0;
    long long end = 0;
    int ruler = 1;
    unsigned number = 0;
};

struct Source {
    std::string id;
    uint64_t numId = 0;
    std::string name;
    std::string path;
    int sampleRate = 48000;
    int channels = 2;
    long long frames = 0;
    FileFormat format = FileFormat::Wav;
    uint64_t timeReference = 0;
    bool unsaved = false;
    std::vector<float> monoPcm;
};

// after SoundCraft (craft-audio AudioBuffer)
struct AudioBuffer {
    uint32_t sampleRate = 48000;
    std::vector<std::vector<float>> channels;

    size_t frames() const { return channels.empty() ? 0 : channels[0].size(); }
    uint16_t numChannels() const { return static_cast<uint16_t>(channels.size()); }
};

// after SoundCraft (crates/model/src/source.rs SourceAudio)
struct SourceAudio {
    AudioBuffer buffer;
    // Per-channel min/max pairs (256 frames per bucket) for fast waveform overview rendering.
    std::vector<std::vector<float>> peaks;

    SourceAudio() = default;
    explicit SourceAudio(AudioBuffer buf);
    size_t frames() const { return buffer.frames(); }
    const std::vector<float>* channel(size_t ch) const { return ch < buffer.channels.size() ? &buffer.channels[ch] : nullptr; }
};

// after SoundCraft (crates/model/src/source.rs SourcePool)
struct SourcePool {
    std::map<uint64_t, std::shared_ptr<SourceAudio>> map;

    void insert(uint64_t id, std::shared_ptr<SourceAudio> audio) { map[id] = std::move(audio); }
    std::shared_ptr<SourceAudio> get(uint64_t id) const {
        auto it = map.find(id);
        return it != map.end() ? it->second : nullptr;
    }
    std::shared_ptr<SourceAudio> remove(uint64_t id) {
        auto it = map.find(id);
        if (it == map.end()) return nullptr;
        auto out = std::move(it->second);
        map.erase(it);
        return out;
    }
    bool contains(uint64_t id) const { return map.find(id) != map.end(); }
    size_t len() const { return map.size(); }
    bool isEmpty() const { return map.empty(); }
    std::vector<uint64_t> ids() const {
        std::vector<uint64_t> out;
        out.reserve(map.size());
        for (const auto& kv : map) out.push_back(kv.first);
        return out;
    }
    bool operator==(const SourcePool& other) const {
        if (map.size() != other.map.size()) return false;
        for (auto it1 = map.begin(), it2 = other.map.begin(); it1 != map.end(); ++it1, ++it2) {
            if (it1->first != it2->first || it1->second.get() != it2->second.get()) return false;
        }
        return true;
    }
};

struct SessionEdit {
    Time::Range selection;
    Time::Range timelineSelection;
    std::vector<uint64_t> selectedTracks;
    std::vector<uint64_t> selectedClips;
    std::vector<uint64_t> selectedSources;
    std::map<std::string, double> values;
    std::map<std::string, bool> flags;
};

struct ClipboardMarker {
    std::string name;
    long long start = 0;
    long long end = 0;
    int ruler = 1;
    std::string comments;
};

struct AutoRenameScheme {
    std::string pattern;
    uint64_t number = 0;
};

struct EditSettings {
    bool automationFollowsEdit = true;
    bool gridClipsMarkers = false;
    bool gridFollowMain = false;
    bool nudgeFollowMain = false;
    bool gridLines = true;
    TimeGrid::GridValue grid = TimeGrid::GridValue::defaultGrid();
    TimeGrid::GridValue nudge = TimeGrid::GridValue::seconds(1.0);
};

struct Document {
    int version = 1;
    uint32_t touch = 0;
    std::string name = "Untitled";
    double sampleRate = 48000.0;
    Time::FrameRate frameRate = Time::FrameRate::Fps30;
    long long timecodeStart = 0;
    long long feetFramesStart = 0;
    Time::Map time;
    EditSettings edit;
    std::vector<Source> sources;
    std::vector<Track> tracks;
    std::vector<Marker> markers;
    std::string folder;
    uint64_t nextClipId = 1;
    uint64_t nextTrackId = 1;
    uint64_t nextSourceId = 1;
    uint64_t nextAlloc = 1;
    SessionEdit sessionEdit;
    std::map<uint64_t, AutoRenameScheme> autoNames;
    SourcePool pool;
};

uint64_t allocId(Document& doc);
std::array<uint8_t, 3> nextTrackColor(const Document& doc);
std::string uniqueTrackName(const Document& doc, const std::string& base);
uint64_t addTrack(Document& doc, TrackKind kind, ChannelFormat format, std::optional<std::string> name = std::nullopt);
Source* findSourceByNum(Document& doc, uint64_t numId);
const Source* findSourceByNum(const Document& doc, uint64_t numId);
std::vector<const Clip*> clipsUsing(const Document& doc, uint64_t sourceNumId);

Document empty();
Document demo();
Track* findTrack(Document& doc, const std::string& id);
const Track* findTrack(const Document& doc, const std::string& id);
Source* findSource(Document& doc, const std::string& id);
const Source* findSource(const Document& doc, const std::string& id);

std::string newClipId(Document& doc);
uint64_t newClipNumId(Document& doc);
void sortPlaylist(Track& track);
Clip* clipMut(Document& doc, const std::string& trackId, const std::string& clipId);
Clip* clipMut(Document& doc, const std::string& clipId);
std::optional<std::pair<std::string, const Clip*>> findClip(const Document& doc, const std::string& clipId);
std::optional<std::pair<std::string, Clip*>> findClip(Document& doc, const std::string& clipId);
bool trackShown(const Document& doc, const Track& track);
long long sourceFrames(const Document& doc, const std::string& sourceId);

// Test / demo helper: append one mono audio track and return its id.
std::string addAudioTrack(Document& doc, const char* name = nullptr);
uint64_t addAudioTrackNum(Document& doc, const char* name = nullptr);

Track* findTrackByNum(Document& doc, uint64_t numId);
const Track* findTrackByNum(const Document& doc, uint64_t numId);
Track* findTrackByName(Document& doc, const std::string& name);
const Track* findTrackByName(const Document& doc, const std::string& name);
std::optional<std::pair<std::string, const Clip*>> findClipByNum(const Document& doc, uint64_t numId);
std::optional<std::pair<std::string, Clip*>> findClipByNum(Document& doc, uint64_t numId);
long long contentEnd(const Document& doc);
bool documentsEqual(const Document& a, const Document& b);
Document demoEngineSession();

} // namespace Session