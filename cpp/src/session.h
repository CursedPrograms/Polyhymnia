// Polyhymnia - session document: tracks, clips, markers, sources (after SoundCraft crates/model).
#pragma once
#include "tempo.h"
#include "time_grid.h"
#include "time_range.h"
#include "timecode.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Session {

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
    std::string path;
    int sampleRate = 48000;
    int channels = 2;
    long long frames = 0;
    std::vector<float> monoPcm;
};

struct SessionEdit {
    Time::Range selection;
    Time::Range timelineSelection;
    std::vector<uint64_t> selectedTracks;
    std::vector<uint64_t> selectedClips;
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
};

uint64_t allocId(Document& doc);

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