// Polyhymnia - in-memory session; see session.h.
#include "session.h"
#include "clip_ops.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Session {

// after SoundCraft (crates/model/src/track.rs TRACK_COLORS)
const std::array<std::array<uint8_t, 3>, 16> kTrackColors = {{
    {86, 132, 214},
    {94, 170, 214},
    {80, 190, 168},
    {104, 186, 98},
    {168, 196, 76},
    {222, 196, 72},
    {230, 152, 62},
    {222, 104, 72},
    {214, 82, 110},
    {196, 86, 168},
    {150, 98, 206},
    {110, 104, 214},
    {130, 150, 170},
    {178, 140, 110},
    {120, 170, 140},
    {190, 120, 150},
}};

// after SoundCraft (crates/model/src/source.rs FileFormat)
const char* fileFormatName(FileFormat f) {
    switch (f) {
    case FileFormat::Wav: return "Wav";
    case FileFormat::Aiff: return "Aiff";
    case FileFormat::Flac: return "Flac";
    case FileFormat::Mp3: return "Mp3";
    case FileFormat::Ogg: return "Ogg";
    case FileFormat::Aac: return "Aac";
    case FileFormat::Alac: return "Alac";
    case FileFormat::Caf: return "Caf";
    case FileFormat::Other: return "Other";
    }
    return "Other";
}

std::optional<FileFormat> fileFormatFromName(const std::string& name) {
    static const std::pair<const char*, FileFormat> kFormats[] = {
        {"Wav", FileFormat::Wav},   {"Aiff", FileFormat::Aiff}, {"Flac", FileFormat::Flac},
        {"Mp3", FileFormat::Mp3},   {"Ogg", FileFormat::Ogg},   {"Aac", FileFormat::Aac},
        {"Alac", FileFormat::Alac}, {"Caf", FileFormat::Caf},   {"Other", FileFormat::Other},
    };
    for (const auto& kv : kFormats) {
        if (name == kv.first) return kv.second;
    }
    return std::nullopt;
}

// after SoundCraft (crates/model/src/mixer.rs ChannelFormat::channels)
size_t channelFormatChannels(ChannelFormat f) {
    switch (f) {
    case ChannelFormat::Mono: return 1;
    case ChannelFormat::Stereo: return 2;
    case ChannelFormat::Lcr: return 3;
    case ChannelFormat::Quad:
    case ChannelFormat::Lcrs: return 4;
    case ChannelFormat::Surround50: return 5;
    case ChannelFormat::Surround51:
    case ChannelFormat::Surround60: return 6;
    case ChannelFormat::Surround61:
    case ChannelFormat::Sdds70:
    case ChannelFormat::Surround70:
    case ChannelFormat::Atmos502: return 7;
    case ChannelFormat::Sdds71:
    case ChannelFormat::Surround71:
    case ChannelFormat::Atmos512: return 8;
    case ChannelFormat::Atmos702:
    case ChannelFormat::Atmos504: return 9;
    case ChannelFormat::Atmos712:
    case ChannelFormat::Atmos514: return 10;
    case ChannelFormat::Atmos704: return 11;
    case ChannelFormat::Atmos714: return 12;
    case ChannelFormat::Atmos706:
    case ChannelFormat::Atmos904: return 13;
    case ChannelFormat::Atmos716:
    case ChannelFormat::Atmos914: return 14;
    case ChannelFormat::Atmos906: return 15;
    case ChannelFormat::Atmos916: return 16;
    case ChannelFormat::Ambisonics1: return 4;
    case ChannelFormat::Ambisonics2: return 9;
    case ChannelFormat::Ambisonics3: return 16;
    case ChannelFormat::Ambisonics4: return 25;
    case ChannelFormat::Ambisonics5: return 36;
    case ChannelFormat::Ambisonics6: return 49;
    case ChannelFormat::Ambisonics7: return 64;
    }
    return 2;
}

// after SoundCraft (crates/model/src/mixer.rs ChannelFormat::label)
const char* channelFormatLabel(ChannelFormat f) {
    switch (f) {
    case ChannelFormat::Mono: return "Mono";
    case ChannelFormat::Stereo: return "Stereo";
    case ChannelFormat::Lcr: return "LCR";
    case ChannelFormat::Quad: return "Quad";
    case ChannelFormat::Lcrs: return "LCRS";
    case ChannelFormat::Surround50: return "5.0";
    case ChannelFormat::Surround51: return "5.1";
    case ChannelFormat::Surround60: return "6.0";
    case ChannelFormat::Surround61: return "6.1";
    case ChannelFormat::Sdds70: return "7.0 SDDS";
    case ChannelFormat::Sdds71: return "7.1 SDDS";
    case ChannelFormat::Surround70: return "7.0";
    case ChannelFormat::Surround71: return "7.1";
    case ChannelFormat::Atmos702: return "7.0.2";
    case ChannelFormat::Atmos712: return "7.1.2";
    case ChannelFormat::Atmos502: return "5.0.2";
    case ChannelFormat::Atmos512: return "5.1.2";
    case ChannelFormat::Atmos504: return "5.0.4";
    case ChannelFormat::Atmos514: return "5.1.4";
    case ChannelFormat::Atmos704: return "7.0.4";
    case ChannelFormat::Atmos714: return "7.1.4";
    case ChannelFormat::Atmos706: return "7.0.6";
    case ChannelFormat::Atmos716: return "7.1.6";
    case ChannelFormat::Atmos904: return "9.0.4";
    case ChannelFormat::Atmos914: return "9.1.4";
    case ChannelFormat::Atmos906: return "9.0.6";
    case ChannelFormat::Atmos916: return "9.1.6";
    case ChannelFormat::Ambisonics1: return "1st Order Ambisonics";
    case ChannelFormat::Ambisonics2: return "2nd Order Ambisonics";
    case ChannelFormat::Ambisonics3: return "3rd Order Ambisonics";
    case ChannelFormat::Ambisonics4: return "4th Order Ambisonics";
    case ChannelFormat::Ambisonics5: return "5th Order Ambisonics";
    case ChannelFormat::Ambisonics6: return "6th Order Ambisonics";
    case ChannelFormat::Ambisonics7: return "7th Order Ambisonics";
    }
    return "Stereo";
}

// after SoundCraft (crates/model/src/mixer.rs ChannelFormat::from_id)
std::optional<ChannelFormat> channelFormatFromId(const std::string& raw) {
    size_t a = 0, b = raw.size();
    while (a < b && std::isspace((unsigned char)raw[a])) ++a;
    while (b > a && std::isspace((unsigned char)raw[b - 1])) --b;
    const std::string s = raw.substr(a, b - a);
    auto eqIgnoreCase = [](const std::string& x, const char* y) {
        size_t i = 0;
        for (; i < x.size() && y[i]; ++i) {
            if (std::tolower((unsigned char)x[i]) != std::tolower((unsigned char)y[i])) return false;
        }
        return i == x.size() && y[i] == '\0';
    };
    static const ChannelFormat kAll[] = {
        ChannelFormat::Mono,        ChannelFormat::Stereo,      ChannelFormat::Lcr,         ChannelFormat::Quad,
        ChannelFormat::Lcrs,        ChannelFormat::Surround50,  ChannelFormat::Surround51,  ChannelFormat::Surround60,
        ChannelFormat::Surround61,  ChannelFormat::Sdds70,      ChannelFormat::Sdds71,      ChannelFormat::Surround70,
        ChannelFormat::Surround71,  ChannelFormat::Atmos702,    ChannelFormat::Atmos712,    ChannelFormat::Ambisonics1,
        ChannelFormat::Ambisonics2, ChannelFormat::Ambisonics3, ChannelFormat::Ambisonics4, ChannelFormat::Ambisonics5,
        ChannelFormat::Ambisonics6, ChannelFormat::Ambisonics7, ChannelFormat::Atmos502,    ChannelFormat::Atmos512,
        ChannelFormat::Atmos504,    ChannelFormat::Atmos514,    ChannelFormat::Atmos704,    ChannelFormat::Atmos714,
        ChannelFormat::Atmos706,    ChannelFormat::Atmos716,    ChannelFormat::Atmos904,    ChannelFormat::Atmos914,
        ChannelFormat::Atmos906,    ChannelFormat::Atmos916,
    };
    for (ChannelFormat f : kAll) {
        if (eqIgnoreCase(s, channelFormatLabel(f))) return f;
    }
    return std::nullopt;
}

// after SoundCraft (crates/model/src/mixer.rs ChannelFormat::for_channels)
ChannelFormat channelFormatForChannels(size_t n) {
    static const ChannelFormat kMap[] = {
        ChannelFormat::Mono,       ChannelFormat::Stereo,     ChannelFormat::Lcr,        ChannelFormat::Quad,
        ChannelFormat::Surround50, ChannelFormat::Surround51, ChannelFormat::Surround70, ChannelFormat::Surround71,
    };
    for (ChannelFormat f : kMap) {
        if (channelFormatChannels(f) == n) return f;
    }
    return n <= 1 ? ChannelFormat::Mono : ChannelFormat::Stereo;
}

// after SoundCraft (crates/model/src/track.rs TrackKind)
const char* trackKindDefaultName(TrackKind k) {
    switch (k) {
    case TrackKind::Audio: return "Audio";
    case TrackKind::Aux: return "Aux";
    case TrackKind::Master: return "Master";
    case TrackKind::Midi: return "MIDI";
    case TrackKind::Instrument: return "Inst";
    case TrackKind::Vca: return "VCA";
    case TrackKind::Folder: return "Folder";
    case TrackKind::Video: return "Video";
    }
    return "Audio";
}

const char* trackKindLabel(TrackKind k) {
    switch (k) {
    case TrackKind::Audio: return "Audio Track";
    case TrackKind::Aux: return "Aux Input";
    case TrackKind::Master: return "Master Fader";
    case TrackKind::Midi: return "MIDI Track";
    case TrackKind::Instrument: return "Instrument Track";
    case TrackKind::Vca: return "VCA Master";
    case TrackKind::Folder: return "Routing Folder Track";
    case TrackKind::Video: return "Video Track";
    }
    return "Audio Track";
}

const char* trackKindId(TrackKind k) {
    switch (k) {
    case TrackKind::Audio: return "audio";
    case TrackKind::Aux: return "aux";
    case TrackKind::Master: return "master";
    case TrackKind::Midi: return "midi";
    case TrackKind::Instrument: return "instrument";
    case TrackKind::Vca: return "vca";
    case TrackKind::Folder: return "folder";
    case TrackKind::Video: return "video";
    }
    return "audio";
}

std::optional<TrackKind> trackKindFromId(const std::string& raw) {
    std::string s = raw;
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    static const TrackKind kAll[] = {TrackKind::Audio,      TrackKind::Aux, TrackKind::Master, TrackKind::Midi,
                                     TrackKind::Instrument, TrackKind::Vca, TrackKind::Folder, TrackKind::Video};
    for (TrackKind k : kAll) {
        std::string lbl = trackKindLabel(k);
        for (char& ch : lbl) ch = (char)std::tolower((unsigned char)ch);
        if (s == trackKindId(k) || s == lbl) return k;
    }
    return std::nullopt;
}

// after SoundCraft (crates/model/src/source.rs SourceAudio::new)
SourceAudio::SourceAudio(AudioBuffer buf) : buffer(std::move(buf)) {
    peaks.resize(buffer.channels.size());
    constexpr size_t kStep = 256;
    for (size_t ch = 0; ch < buffer.channels.size(); ++ch) {
        const auto& c = buffer.channels[ch];
        const size_t nBuckets = (c.size() + kStep - 1) / kStep;
        peaks[ch].resize(nBuckets * 2, 0.0f);
        for (size_t b = 0; b < nBuckets; ++b) {
            const size_t start = b * kStep;
            const size_t end = std::min(c.size(), start + kStep);
            float mn = 1.0f, mx = -1.0f;
            for (size_t i = start; i < end; ++i) {
                mn = std::min(mn, c[i]);
                mx = std::max(mx, c[i]);
            }
            if (end <= start) {
                mn = 0.0f;
                mx = 0.0f;
            }
            peaks[ch][b * 2] = mn;
            peaks[ch][b * 2 + 1] = mx;
        }
    }
}

bool AutoParam::operator<(const AutoParam& o) const {
    if (kind != o.kind) return kind < o.kind;
    return index < o.index;
}

// after SoundCraft (crates/model/src/automation.rs AutomationLane::set_point)
void AutomationLane::setPoint(long long at, float value) {
    float lo = -144.0f, hi = 12.0f;
    if (param.kind == AutoParamKind::Pan || param.kind == AutoParamKind::Mute) {
        lo = -1.0f;
        hi = 1.0f;
    }
    if (std::isnan(value)) value = 0.0f;
    value = std::clamp(value, lo, hi);
    const auto it = std::lower_bound(points.begin(), points.end(), at, [](const AutomationPoint& p, long long t) { return p.at < t; });
    if (it != points.end() && it->at == at)
        it->value = value;
    else
        points.insert(it, {at, value});
}

// after SoundCraft (crates/model/src/automation.rs AutomationLane::clear_range)
void AutomationLane::clearRange(long long start, long long end) {
    points.erase(std::remove_if(points.begin(), points.end(), [&](const AutomationPoint& p) { return p.at >= start && p.at < end; }),
                 points.end());
}

float AutomationLane::valueAt(long long at, float fallback) const {
    if (points.empty()) return fallback;
    const auto it = std::upper_bound(points.begin(), points.end(), at, [](long long t, const AutomationPoint& p) { return t < p.at; });
    if (it == points.begin()) return it->value;
    if (it == points.end()) return points.back().value;
    const auto& b = *it;
    const auto& a = *(it - 1);
    if (b.at == a.at) return b.value;
    const double t = static_cast<double>(at - a.at) / static_cast<double>(b.at - a.at);
    return static_cast<float>(a.value + t * (b.value - a.value));
}

AutomationLane* Track::laneMut(const AutoParam& p) {
    for (auto& l : automation)
        if (l.param == p) return &l;
    automation.push_back({p, {}, true});
    return &automation.back();
}

const AutomationLane* Track::lane(const AutoParam& p) const {
    for (const auto& l : automation)
        if (l.param == p) return &l;
    return nullptr;
}

Document empty() {
    Document d;
    d.name = "Untitled";
    d.touch = 1;
    return d;
}

Document demo() {
    Document d;
    d.name = "Demo";
    d.time.tempoBpm = 120.0;
    d.time.meter = {4, 4};
    Session::Source sine;
    sine.numId = d.nextSourceId++;
    sine.id = "sine";
    sine.name = "sine_440";
    sine.path = "audio/sine_440.wav";
    sine.sampleRate = 48000;
    sine.channels = 1;
    sine.frames = 48000 * 4;
    d.sources.push_back(sine);
    Session::Source click;
    click.numId = d.nextSourceId++;
    click.id = "click";
    click.name = "click";
    click.path = "audio/click.wav";
    click.sampleRate = 48000;
    click.channels = 1;
    click.frames = 48000;
    d.sources.push_back(click);
    Track a;
    a.numId = d.nextTrackId++;
    a.id = "t1";
    a.name = "Lead";
    a.color = kTrackColors[0];
    Session::Clip c1;
    c1.numId = newClipNumId(d);
    c1.id = "c1";
    c1.name = "Lead clip";
    c1.sourceId = "sine";
    c1.start = 0;
    c1.length = 48000 * 2;
    c1.gainDb = -3.0f;
    a.clips.push_back(c1);
    d.tracks.push_back(a);
    Track b;
    b.numId = d.nextTrackId++;
    b.id = "t2";
    b.name = "Click";
    b.color = kTrackColors[1];
    b.gainDb = -6.0f;
    Session::Clip c2;
    c2.numId = newClipNumId(d);
    c2.id = "c2";
    c2.name = "Click clip";
    c2.sourceId = "click";
    c2.start = 48000;
    c2.length = 48000;
    b.clips.push_back(c2);
    d.tracks.push_back(b);
    d.markers.push_back({"m1", "Intro", 0, 0, 1, 1});
    d.markers.push_back({"m2", "Verse", 48000, 48000, 1, 2});
    d.touch = 1;
    return d;
}

Track* findTrack(Document& doc, const std::string& id) {
    for (auto& t : doc.tracks)
        if (t.id == id) return &t;
    return nullptr;
}

const Track* findTrack(const Document& doc, const std::string& id) {
    for (const auto& t : doc.tracks)
        if (t.id == id) return &t;
    return nullptr;
}

Source* findSource(Document& doc, const std::string& id) {
    for (auto& s : doc.sources)
        if (s.id == id) return &s;
    return nullptr;
}

const Source* findSource(const Document& doc, const std::string& id) {
    for (const auto& s : doc.sources)
        if (s.id == id) return &s;
    return nullptr;
}

std::string newClipId(Document& doc) {
    const uint64_t n = doc.nextClipId++;
    std::string id = "clip_" + std::to_string(n);
    return id;
}

uint64_t newClipNumId(Document& doc) { return doc.nextClipId++; }

uint64_t allocId(Document& doc) { return doc.nextAlloc++; }

long long Clip::end() const { return start + length; }

std::vector<Clip>& Track::activeClips() {
    if (activePlaylist == 0) return clips;
    const size_t idx = activePlaylist - 1;
    if (idx >= playlists.size()) playlists.resize(idx + 1);
    return playlists[idx].clips;
}

const std::vector<Clip>& Track::activeClips() const {
    if (activePlaylist == 0) return clips;
    const size_t idx = activePlaylist - 1;
    if (idx >= playlists.size()) return clips;
    return playlists[idx].clips;
}

Track* findTrackByNum(Document& doc, uint64_t numId) {
    for (auto& t : doc.tracks)
        if (t.numId == numId) return &t;
    return nullptr;
}

const Track* findTrackByNum(const Document& doc, uint64_t numId) {
    for (const auto& t : doc.tracks)
        if (t.numId == numId) return &t;
    return nullptr;
}

Track* findTrackByName(Document& doc, const std::string& name) {
    for (auto& t : doc.tracks)
        if (t.name == name) return &t;
    return nullptr;
}

const Track* findTrackByName(const Document& doc, const std::string& name) {
    for (const auto& t : doc.tracks)
        if (t.name == name) return &t;
    return nullptr;
}

std::optional<std::pair<std::string, const Clip*>> findClipByNum(const Document& doc, uint64_t numId) {
    for (const auto& t : doc.tracks) {
        for (const auto& c : t.clips)
            if (c.numId == numId) return std::pair{t.id, &c};
        for (const auto& pl : t.playlists)
            for (const auto& c : pl.clips)
                if (c.numId == numId) return std::pair{t.id, &c};
    }
    return std::nullopt;
}

std::optional<std::pair<std::string, Clip*>> findClipByNum(Document& doc, uint64_t numId) {
    for (auto& t : doc.tracks) {
        for (auto& c : t.clips)
            if (c.numId == numId) return std::pair{t.id, &c};
        for (auto& pl : t.playlists)
            for (auto& c : pl.clips)
                if (c.numId == numId) return std::pair{t.id, &c};
    }
    return std::nullopt;
}

long long contentEnd(const Document& doc) {
    long long end = 0;
    for (const auto& t : doc.tracks) {
        for (const auto& c : t.clips) end = std::max(end, c.end());
        for (const auto& pl : t.playlists)
            for (const auto& c : pl.clips) end = std::max(end, c.end());
    }
    return end;
}

uint64_t addAudioTrackNum(Document& doc, const char* name) {
    Track t;
    t.numId = doc.nextTrackId++;
    t.id = "tr_" + std::to_string(t.numId);
    t.name = name ? name : "Audio";
    t.kind = TrackKind::Audio;
    doc.tracks.push_back(std::move(t));
    return doc.tracks.back().numId;
}

static bool clipsEqual(const Clip& a, const Clip& b) {
    if (a.numId != b.numId || a.start != b.start || a.length != b.length || a.offset != b.offset) return false;
    if (a.gainDb != b.gainDb || a.syncPoint != b.syncPoint || a.stretch != b.stretch) return false;
    if (a.fadeIn.len != b.fadeIn.len || a.fadeOut.len != b.fadeOut.len) return false;
    if (a.fadeIn.shape != b.fadeIn.shape || a.fadeOut.shape != b.fadeOut.shape) return false;
    if (std::abs(a.fadeIn.curvature - b.fadeIn.curvature) > 1e-6f || std::abs(a.fadeOut.curvature - b.fadeOut.curvature) > 1e-6f) return false;
    return true;
}

bool documentsEqual(const Document& a, const Document& b) {
    if (a.sessionEdit.selection.start != b.sessionEdit.selection.start || a.sessionEdit.selection.end != b.sessionEdit.selection.end)
        return false;
    if (a.sessionEdit.selectedTracks != b.sessionEdit.selectedTracks) return false;
    if (a.sessionEdit.selectedClips != b.sessionEdit.selectedClips) return false;
    if (a.sessionEdit.selectedSources != b.sessionEdit.selectedSources) return false;
    if (a.tracks.size() != b.tracks.size()) return false;
    if (a.sources.size() != b.sources.size()) return false;
    if (!(a.pool == b.pool)) return false;
    if (a.markers.size() != b.markers.size()) return false;
    for (size_t i = 0; i < a.sources.size(); ++i) {
        if (a.sources[i].numId != b.sources[i].numId || a.sources[i].id != b.sources[i].id ||
            a.sources[i].path != b.sources[i].path || a.sources[i].frames != b.sources[i].frames)
            return false;
    }
    for (size_t i = 0; i < a.tracks.size(); ++i) {
        const auto& ta = a.tracks[i];
        const auto& tb = b.tracks[i];
        if (ta.numId != tb.numId || ta.name != tb.name || ta.kind != tb.kind || ta.format != tb.format) return false;
        if (ta.clips.size() != tb.clips.size() || ta.playlists.size() != tb.playlists.size()) return false;
        for (size_t j = 0; j < ta.clips.size(); ++j)
            if (!clipsEqual(ta.clips[j], tb.clips[j])) return false;
        for (size_t p = 0; p < ta.playlists.size(); ++p) {
            if (ta.playlists[p].clips.size() != tb.playlists[p].clips.size()) return false;
            for (size_t j = 0; j < ta.playlists[p].clips.size(); ++j)
                if (!clipsEqual(ta.playlists[p].clips[j], tb.playlists[p].clips[j])) return false;
        }
    }
    return true;
}

static Source addMonoSource(Document& doc, const std::string& name, std::vector<float> pcm, int sr) {
    Source s;
    s.numId = doc.nextSourceId++;
    s.id = "src_" + std::to_string(s.numId);
    s.name = name;
    s.path = "Audio Files/" + name + ".wav";
    s.sampleRate = sr;
    s.channels = 1;
    s.frames = static_cast<long long>(pcm.size());
    s.format = FileFormat::Wav;
    s.unsaved = true;
    AudioBuffer buf;
    buf.sampleRate = static_cast<uint32_t>(sr);
    buf.channels = {pcm};
    doc.pool.insert(s.numId, std::make_shared<SourceAudio>( std::move(buf)));
    s.monoPcm = std::move(pcm);
    doc.sources.push_back(std::move(s));
    return doc.sources.back();
}

static Clip makeAudioClip(Document& doc, const std::string& sourceId, uint64_t srcNum, long long off, long long start, long long len,
                            const char* clipName) {
    Clip c;
    c.numId = newClipNumId(doc);
    c.id = "clip_" + std::to_string(c.numId);
    c.name = clipName;
    c.sourceId = sourceId;
    (void)srcNum;
    c.offset = off;
    c.start = start;
    c.length = len;
    return c;
}

// after SoundCraft (crates/engine/src/demo.rs demo_session) — trimmed for engine command tests.
Document demoEngineSession() {
    const int sr = 48000;
    Document d;
    d.name = "Midnight Groove";
    d.sampleRate = sr;
    d.time.tempoBpm = 104.0;
    d.time.meter = {4, 4};

    const size_t bar = static_cast<size_t>(sr * 60.0 / 104.0) * 4;
    const size_t total = bar * 8;
    std::vector<float> kick(total, 0.0f);
    for (size_t i = 0; i < total; i += bar / 2) {
        for (size_t j = 0; j < 2000 && i + j < total; ++j) {
            const float env = std::exp(-static_cast<float>(j) / 400.0f);
            kick[i + j] += env * 0.8f;
        }
    }
    const Source kickSrc = addMonoSource(d, "Kick", std::move(kick), sr);

    std::vector<float> pad(total, 0.0f);
    for (size_t i = 0; i < total; ++i) pad[i] = static_cast<float>(0.15 * std::sin(i * 0.01));
    const Source padSrc = addMonoSource(d, "Pad", std::move(pad), sr);

    Track tk;
    tk.numId = d.nextTrackId++;
    tk.id = "tr_kick";
    tk.name = "Kick";
    tk.color = kTrackColors[0];
    for (size_t b = 0; b < 3; ++b) {
        tk.clips.push_back(makeAudioClip(d, kickSrc.id, kickSrc.numId, 0, static_cast<long long>(b * bar * 2),
                                         static_cast<long long>(bar * 2), "Kick"));
    }
    d.tracks.push_back(tk);

    Track tp;
    tp.numId = d.nextTrackId++;
    tp.id = "tr_pad";
    tp.name = "Pad";
    tp.clips.push_back(makeAudioClip(d, padSrc.id, padSrc.numId, 0, 0, static_cast<long long>(total), "Pad"));
    d.tracks.push_back(tp);

    Track keys;
    keys.numId = d.nextTrackId++;
    keys.id = "tr_keys";
    keys.name = "Keys";
    keys.kind = TrackKind::Instrument;
    Clip kc;
    kc.numId = newClipNumId(d);
    kc.id = "clip_" + std::to_string(kc.numId);
    kc.name = "Keys";
    kc.isMidi = true;
    kc.start = static_cast<long long>(bar * 4);
    kc.length = static_cast<long long>(bar * 4);
  kc.midi = MidiSequence{};
    kc.midi->notes.push_back({60, 100, 64, 0, 0, 480});
    keys.clips.push_back(kc);
    d.tracks.push_back(keys);

    d.markers.push_back({"m1", "Intro", static_cast<long long>(bar), static_cast<long long>(bar), 1, 1});
    d.touch = 1;
    return d;
}

namespace {

// after SoundCraft (crates/engine/src/demo.rs Rng, kick, snare, hat, drums, bass, pad, keys_sequence, lead_sequence)
struct DemoRng {
    uint64_t s = 0x9e3779b97f4a7c15ULL;
    float next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<float>(s >> 40) / static_cast<float>(1ULL << 24) * 2.0f - 1.0f;
    }
};

double midiToHz(double note) {
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

void synthKick(double sr, size_t frames, std::vector<float>& out, size_t at, float vel) {
    const size_t len = static_cast<size_t>(sr * 0.45);
    double phase = 0.0;
    const size_t limit = (at < frames) ? std::min(len, frames - at) : 0;
    for (size_t i = 0; i < limit; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double f = 48.0 + 110.0 * std::exp(-t * 28.0);
        phase += 2.0 * 3.14159265358979323846 * f / sr;
        const float env = static_cast<float>(std::exp(-t * 7.5));
        const float click = (i < 64) ? (static_cast<float>(64 - i) / 64.0f * 0.3f) : 0.0f;
        out[at + i] += (static_cast<float>(std::sin(phase)) * env + click) * vel * 0.9f;
    }
}

void synthSnare(double sr, size_t frames, std::vector<float>& out, size_t at, float vel, DemoRng& rng) {
    const size_t len = static_cast<size_t>(sr * 0.3);
    float lp = 0.0f;
    const size_t limit = (at < frames) ? std::min(len, frames - at) : 0;
    for (size_t i = 0; i < limit; ++i) {
        const double t = static_cast<double>(i) / sr;
        const float n = rng.next();
        lp += 0.55f * (n - lp);
        const float body = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 185.0 * t) * std::exp(-t * 22.0));
        const float env = static_cast<float>(std::exp(-t * 14.0));
        out[at + i] += ((n - lp) * env * 0.8f + body * 0.5f) * vel * 0.7f;
    }
}

void synthHat(double sr, size_t frames, std::vector<float>& out, size_t at, float vel, bool open, DemoRng& rng) {
    const size_t len = static_cast<size_t>(sr * (open ? 0.35 : 0.06));
    float prev = 0.0f;
    const size_t limit = (at < frames) ? std::min(len, frames - at) : 0;
    for (size_t i = 0; i < limit; ++i) {
        const double t = static_cast<double>(i) / sr;
        const float n = rng.next();
        const float hp = n - prev;
        prev = n;
        const float env = static_cast<float>(std::exp(-t * (open ? 9.0 : 70.0)));
        out[at + i] += hp * env * vel * 0.25f;
    }
}

constexpr uint8_t kProg[4][4] = {{57, 60, 64, 69}, {53, 57, 60, 65}, {48, 52, 55, 60}, {55, 59, 62, 67}};

} // namespace

// after SoundCraft (crates/engine/src/demo.rs demo_session)
Document midnightGrooveDemo() {
    static std::optional<Document> cached;
    if (cached) return *cached;

    constexpr double kBpm = 104.0;
    constexpr size_t kBars = 24;
    const double sr = 48000.0;
    const size_t beat = static_cast<size_t>( std::llround(sr * 60.0 / kBpm));
    const size_t bar = beat * 4;
    const size_t total = bar * kBars + static_cast<size_t>(sr * 2.0);

    Document d;
    d.name = "Midnight Groove";
    d.sampleRate = sr;
    d.bitDepth = 24;
    d.time.tempoBpm = kBpm;
    d.time.meter = {4, 4};

    std::vector<float> kBuf(total, 0.0f), sBuf(total, 0.0f), hBuf(total, 0.0f);
    DemoRng rng;
    for (size_t b = 0; b < kBars; ++b) {
        const size_t b0 = b * bar;
        const bool full = b >= 4;
        if (full) {
            synthKick(sr, total, kBuf, b0, 1.0f);
            synthKick(sr, total, kBuf, b0 + 2 * beat + beat / 2, 0.8f);
            synthKick(sr, total, kBuf, b0 + 3 * beat, 0.6f);
            synthSnare(sr, total, sBuf, b0 + beat, 1.0f, rng);
            synthSnare(sr, total, sBuf, b0 + 3 * beat, 1.0f, rng);
            if (b % 4 == 3) {
                synthSnare(sr, total, sBuf, b0 + 3 * beat + beat / 2, 0.5f, rng);
                synthSnare(sr, total, sBuf, b0 + 3 * beat + 3 * beat / 4, 0.7f, rng);
            }
        }
        for (size_t i = 0; i < 8; ++i) {
            synthHat(sr, total, hBuf, b0 + i * beat / 2, (i % 2 == 0) ? 1.0f : 0.6f, full && i == 7, rng);
        }
    }

    std::vector<float> bassBuf(total, 0.0f);
    double lp = 0.0;
    for (size_t b = 4; b < kBars; ++b) {
        const double root = static_cast<double>(kProg[b % 4][0]) - 24.0;
        const std::tuple<size_t, double, double> pat[5] = {
            {0, 0.0, 1.5}, {2, 0.0, 0.5}, {3, 12.0, 0.5}, {5, 0.0, 0.5}, {6, 7.0, 1.0}};
        for (const auto& [eighth, semi, beats] : pat) {
            const size_t at = b * bar + eighth * beat / 2;
            const size_t len = static_cast<size_t>(beats * static_cast<double>(beat));
            const double f = midiToHz(root + semi);
            double ph = 0.0;
            for (size_t i = 0; i < len && at + i < total; ++i) {
                ph = std::fmod(ph + f / sr, 1.0);
                const double saw = 2.0 * ph - 1.0;
                const double t = static_cast<double>(i) / sr;
                const double cutoff = 0.02 + 0.12 * std::exp(-t * 9.0);
                lp += cutoff * (saw - lp);
                const double env = (1.0 - std::exp(-t * 200.0)) * ((i + 400 > len) ? static_cast<double>(len - i) / 400.0 : 1.0);
                bassBuf[at + i] += static_cast<float>(lp * env * 0.55);
            }
        }
    }

    std::vector<float> padL(total, 0.0f), padR(total, 0.0f);
    for (size_t b = 0; b < kBars; ++b) {
        const auto& chord = kProg[b % 4];
        const size_t at = b * bar;
        for (size_t vi = 0; vi < 4; ++vi) {
            const double f = midiToHz(static_cast<double>(chord[vi]) + 12.0);
            for (int side : {-1, 1}) {
                std::vector<float>& dst = (side < 0) ? padL : padR;
                const double det = 1.0 + static_cast<double>(side) * 0.0035 * static_cast<double>(vi + 1);
                double ph = static_cast<double>(vi) * 0.17;
                for (size_t i = 0; i < bar && at + i < total; ++i) {
                    ph = std::fmod(ph + f * det / sr, 1.0);
                    const double tri = 1.0 - 4.0 * std::abs(ph - 0.5);
                    const double t = static_cast<double>(i) / static_cast<double>(bar);
                    const double env = std::min(1.0, t * 8.0) * std::min(1.0, (1.0 - t) * 6.0);
                    dst[at + i] += static_cast<float>(tri * env * 0.07);
                }
            }
        }
    }

    auto addDemoAudioTrack = [&](const char* name, std::vector<std::vector<float>> chans, size_t startBar, size_t colorIdx,
                                 float volDb, float panVal, const char* inserts, const std::vector<size_t>& splits) {
        const size_t nCh = chans.size();
        Source s;
        s.numId = d.nextSourceId++;
        s.id = "src_" + std::to_string(s.numId);
        s.name = name;
        s.path = std::string("Audio Files/") + name + ".wav";
        s.sampleRate = static_cast<int>(sr);
        s.channels = static_cast<int>(nCh);
        s.frames = static_cast<long long>(chans[0].size());
        s.format = FileFormat::Wav;
        s.unsaved = true;
        s.monoPcm = chans[0];
        AudioBuffer ab;
        ab.sampleRate = static_cast<uint32_t>(sr);
        ab.channels = std::move(chans);
        d.pool.insert(s.numId, std::make_shared<SourceAudio>(std::move(ab)));
        d.sources.push_back(std::move(s));
        const std::string srcId = d.sources.back().id;
        const uint64_t srcNum = d.sources.back().numId;

        const uint64_t tNum = addTrack(d, TrackKind::Audio, channelFormatForChannels(nCh), std::string(name));
        Track* tr = findTrackByNum(d, tNum);
        tr->color = kTrackColors[colorIdx % 16];
        tr->gainDb = volDb;
        tr->pan = panVal;
        if (tr->mixer.pan.size() == 1) tr->mixer.pan[0] = panVal;
        tr->view = "waveform";
        tr->insertsSummary = inserts ? inserts : "";

        const long long startSamp = static_cast<long long>(startBar * bar);
        const long long fullEnd = static_cast<long long>(total) - static_cast<long long>(sr * 1.5);
        std::vector<long long> bounds = {startSamp};
        for (size_t sp : splits) bounds.push_back(static_cast<long long>(sp * bar));
        bounds.push_back(fullEnd);
        for (size_t i = 0; i + 1 < bounds.size(); ++i) {
            Clip c = makeAudioClip(d, srcId, srcNum, bounds[i], bounds[i], bounds[i + 1] - bounds[i], name);
            if (i + 2 == bounds.size()) {
                c.fadeOut = {static_cast<long long>(sr * 0.5), FadeShape::SCurve, 0.0f};
            }
            tr->clips.push_back(std::move(c));
        }
        return tNum;
    };

    const uint64_t verbBus = addBus(d, "Verb", ChannelFormat::Stereo);
    const uint64_t tk = addDemoAudioTrack("Kick", {std::move(kBuf)}, 4, 7, -3.0f, 0.0f, "EQ7 \xC2\xB7 Comp", {12, 20});
    const uint64_t ts = addDemoAudioTrack("Snare", {std::move(sBuf)}, 4, 6, -4.0f, 0.0f, "", {12, 20});
    const uint64_t th = addDemoAudioTrack("Hats", {std::move(hBuf)}, 0, 5, -10.0f, 0.35f, "", {});
    const uint64_t tb = addDemoAudioTrack("Bass", {std::move(bassBuf)}, 4, 10, -5.0f, 0.0f, "Comp \xC2\xB7 Satur", {8, 16});
    const uint64_t tp = addDemoAudioTrack("Pad", {std::move(padL), std::move(padR)}, 0, 1, -8.0f, -1.0f, "Chorus", {});
    (void)th;
    if (Track* kickTr = findTrackByNum(d, tk)) {
        kickTr->mixer.inserts[0] = Insert::make("eq_7band");
        kickTr->mixer.inserts[1] = Insert::make("compressor");
    }
    if (Track* snareTr = findTrackByNum(d, ts)) {
        snareTr->mixer.sends[0] = SendSlot::make(Route::bus(verbBus), -10.0f);
    }
    if (Track* bassTr = findTrackByNum(d, tb)) {
        bassTr->mixer.inserts[0] = Insert::make("compressor");
        bassTr->mixer.inserts[1] = Insert::make("saturator");
    }
    if (Track* padTr = findTrackByNum(d, tp)) {
        padTr->mixer.inserts[0] = Insert::make("chorus");
        padTr->mixer.sends[0] = SendSlot::make(Route::bus(verbBus), -8.0f);
        AutomationLane* l = padTr->laneMut({AutoParamKind::Volume, 0});
        l->setPoint(0, -30.0f);
        l->setPoint(static_cast<long long>(4 * bar), -8.0f);
        l->setPoint(static_cast<long long>(20 * bar), -8.0f);
        l->setPoint(static_cast<long long>(24 * bar), -24.0f);
    }

    // Keys Instrument track
    {
        const uint64_t tKeys = addTrack(d, TrackKind::Instrument, ChannelFormat::Stereo, std::string("Keys"));
        Track* tr = findTrackByNum(d, tKeys);
        tr->color = kTrackColors[2];
        tr->gainDb = -9.0f;
        tr->pan = -1.0f;
        tr->view = "notes";
        tr->instrument = Insert::make("subtractive_synth");
        tr->mixer.sends[0] = SendSlot::make(Route::bus(verbBus), -12.0f);
        Clip kc;
        kc.numId = newClipNumId(d);
        kc.id = "clip_" + std::to_string(kc.numId);
        kc.name = "Keys";
        kc.isMidi = true;
        kc.start = static_cast<long long>(4 * bar);
        kc.length = static_cast<long long>((kBars - 4) * bar);
        kc.midi = MidiSequence{};
        const long long q = 960;
        for (long long b = 0; b < static_cast<long long>(kBars - 4); ++b) {
            const auto& chord = kProg[(b + 4) % 4];
            for (long long offs : {0LL, q + q / 2, 3 * q}) {
                for (size_t ni = 1; ni < 4; ++ni) {
                    kc.midi->notes.push_back({chord[ni], 92, 64, 0, b * 4 * q + offs, q - 40});
                }
            }
        }
        tr->clips.push_back(std::move(kc));
    }

    // Lead Instrument track
    {
        const uint64_t tLead = addTrack(d, TrackKind::Instrument, ChannelFormat::Stereo, std::string("Lead"));
        Track* tr = findTrackByNum(d, tLead);
        tr->color = kTrackColors[9];
        tr->gainDb = -12.0f;
        tr->pan = -1.0f;
        tr->view = "notes";
        tr->instrument = Insert::make("subtractive_synth");
        tr->mixer.sends[0] = SendSlot::make(Route::bus(verbBus), -6.0f);
        Clip lc;
        lc.numId = newClipNumId(d);
        lc.id = "clip_" + std::to_string(lc.numId);
        lc.name = "Lead";
        lc.isMidi = true;
        lc.start = static_cast<long long>(12 * bar);
        lc.length = static_cast<long long>(8 * bar);
        lc.midi = MidiSequence{};
        const long long e = 480;
        const std::tuple<uint8_t, long long, long long> phrase[10] = {
            {76, 0, 3}, {74, 3, 1}, {72, 4, 2}, {74, 6, 2}, {76, 8, 4},
            {79, 12, 2}, {77, 14, 2}, {76, 16, 6}, {72, 22, 2}, {74, 24, 8}};
        for (long long rep = 0; rep < 2; ++rep) {
            for (const auto& [pitch, st, ln] : phrase) {
                lc.midi->notes.push_back({pitch, 100, 64, 0, rep * 32 * e + st * e, ln * e - 30});
            }
        }
        tr->clips.push_back(std::move(lc));
    }

    // Verb Aux track
    {
        const uint64_t tVerb = addTrack(d, TrackKind::Aux, ChannelFormat::Stereo, std::string("Verb"));
        Track* tr = findTrackByNum(d, tVerb);
        tr->color = kTrackColors[12];
        tr->gainDb = -4.0f;
        tr->pan = -1.0f;
        tr->view = "volume";
        tr->insertsSummary = "Plate";
        tr->mixer.input = Route::bus(verbBus);
        tr->mixer.inserts[0] = Insert::make("plate_reverb");
    }

    // Master 1 track
    {
        const uint64_t tMaster = addTrack(d, TrackKind::Master, ChannelFormat::Stereo, std::string("Master"));
        Track* tr = findTrackByNum(d, tMaster);
        tr->color = {140, 140, 140};
        tr->gainDb = 0.0f;
        tr->view = "volume";
        tr->insertsSummary = "Maximizer";
        tr->mixer.inserts[0] = Insert::make("maximizer");
    }

    d.markers.push_back({"m1", "Intro", 0, 0, 1, 1, 0});
    d.markers.push_back({"m2", "Verse", static_cast<long long>(4 * bar), static_cast<long long>(4 * bar), 1, 2, 0});
    d.markers.push_back({"m3", "Chorus", static_cast<long long>(12 * bar), static_cast<long long>(12 * bar), 1, 3, 0});
    d.markers.push_back({"m4", "Outro", static_cast<long long>(20 * bar), static_cast<long long>(20 * bar), 1, 4, 0});

    d.sessionEdit.selectedTracks = {tk};
    d.sessionEdit.selection = Time::Range::newRange(static_cast<long long>(4 * bar), static_cast<long long>(4 * bar));
    (void)tb;
    d.touch = 1;
    cached = d;
    return d;
}

// after SoundCraft (crates/model/src/track.rs Playlist::sort)
void sortPlaylist(Track& track) {
    std::sort(track.clips.begin(), track.clips.end(), [](const Clip& a, const Clip& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.id < b.id;
    });
}

Clip* clipMut(Document& doc, const std::string& trackId, const std::string& clipId) {
    Track* t = findTrack(doc, trackId);
    if (!t) return nullptr;
    for (auto& c : t->clips)
        if (c.id == clipId) return &c;
    return nullptr;
}

Clip* clipMut(Document& doc, const std::string& clipId) {
    for (auto& t : doc.tracks) {
        if (Clip* c = clipMut(doc, t.id, clipId)) return c;
    }
    return nullptr;
}

std::optional<std::pair<std::string, const Clip*>> findClip(const Document& doc, const std::string& clipId) {
    for (const auto& t : doc.tracks)
        for (const auto& c : t.clips)
            if (c.id == clipId) return std::pair{t.id, &c};
    return std::nullopt;
}

std::optional<std::pair<std::string, Clip*>> findClip(Document& doc, const std::string& clipId) {
    for (auto& t : doc.tracks)
        for (auto& c : t.clips)
            if (c.id == clipId) return std::pair{t.id, &c};
    return std::nullopt;
}

// after SoundCraft (crates/model/src/session.rs track_shown)
bool trackShown(const Document& doc, const Track& track) {
    if (track.hidden) return false;
    std::string folder = track.folderParent;
    for (int i = 0; i < 64 && !folder.empty(); ++i) {
        const Track* parent = findTrack(doc, folder);
        if (!parent) return true;
        if (!parent->folderOpen) return false;
        folder = parent->folderParent;
    }
    return true;
}

long long sourceFrames(const Document& doc, const std::string& sourceId) {
    if (const Source* s = findSource(doc, sourceId)) return s->frames;
    return 0;
}

std::string addAudioTrack(Document& doc, const char* name) {
    Track t;
    t.numId = doc.nextTrackId++;
    t.id = "tr_" + std::to_string(t.numId);
    t.name = name ? name : uniqueTrackName(doc, "Audio");
    t.kind = TrackKind::Audio;
    t.format = ChannelFormat::Mono;
    t.color = nextTrackColor(doc);
    doc.tracks.push_back(std::move(t));
    return doc.tracks.back().id;
}

// after SoundCraft (crates/model/src/session.rs Session::next_track_color)
std::array<uint8_t, 3> nextTrackColor(const Document& doc) {
    size_t n = 0;
    for (const auto& t : doc.tracks) {
        if (t.kind != TrackKind::Master) ++n;
    }
    return kTrackColors[n % kTrackColors.size()];
}

// after SoundCraft (crates/model/src/session.rs Session::unique_track_name)
std::string uniqueTrackName(const Document& doc, const std::string& base) {
    auto taken = [&](const std::string& n) {
        for (const auto& t : doc.tracks) {
            if (t.name == n) return true;
        }
        return false;
    };
    const bool isDefaultBase = (base == "Audio" || base == "Aux" || base == "MIDI" || base == "Inst" ||
                                base == "VCA" || base == "Folder" || base == "Master");
    if (!base.empty() && !taken(base) && !isDefaultBase) {
        return base;
    }
    const std::string prefix = base + " ";
    std::unordered_set<uint64_t> used;
    for (const auto& t : doc.tracks) {
        if (t.name.rfind(prefix, 0) == 0) {
            const std::string rest = t.name.substr(prefix.size());
            if (!rest.empty()) {
                try {
                    size_t pos = 0;
                    const uint64_t val = std::stoull(rest, &pos);
                    if (pos == rest.size() && std::to_string(val) == rest) used.insert(val);
                } catch (...) {
                }
            }
        }
    }
    uint64_t i = 1;
    while (used.count(i)) ++i;
    return prefix + std::to_string(i);
}

// after SoundCraft (crates/model/src/mixer.rs fader_pos_to_db)
float faderPosToDb(float pos) {
    const float p = std::isfinite(pos) ? std::clamp(pos, 0.0f, 1.0f) : 0.0f;
    if (p <= 0.0f) return -144.0f;
    if (p >= 0.25f) return -24.0f + (p - 0.25f) / 0.75f * 36.0f;
    return -24.0f - std::pow(1.0f - p / 0.25f, 1.5f) * 120.0f;
}

// after SoundCraft (crates/model/src/mixer.rs fader_db_to_pos)
float faderDbToPos(float db) {
    const float d = std::isfinite(db) ? db : -144.0f;
    if (d <= -144.0f) return 0.0f;
    if (d >= -24.0f) return std::min(1.0f, 0.25f + (std::min(12.0f, d) + 24.0f) / 36.0f * 0.75f);
    const float x = std::pow(std::clamp((-24.0f - d) / 120.0f, 0.0f, 1.0f), 1.0f / 1.5f);
    return std::max(0.0f, (1.0f - x) * 0.25f);
}

// after SoundCraft (crates/model/src/session.rs Session::add_bus)
uint64_t addBus(Document& doc, const std::string& name, ChannelFormat format) {
    Bus b;
    b.numId = doc.busses.size() + 1;
    for (const auto& ex : doc.busses) {
        if (ex.numId >= b.numId) b.numId = ex.numId + 1;
    }
    b.name = name;
    b.format = format;
    doc.busses.push_back(std::move(b));
    return doc.busses.back().numId;
}

const Bus* findBus(const Document& doc, uint64_t busId) {
    for (const auto& b : doc.busses) {
        if (b.numId == busId) return &b;
    }
    return nullptr;
}

const Bus* findBusByName(const Document& doc, const std::string& name) {
    for (const auto& b : doc.busses) {
        if (b.name == name) return &b;
    }
    return nullptr;
}

// after SoundCraft (crates/model/src/session.rs Session::add_track)
uint64_t addTrack(Document& doc, TrackKind kind, ChannelFormat format, std::optional<std::string> name) {
    Track t;
    t.numId = doc.nextTrackId++;
    t.id = "tr_" + std::to_string(t.numId);
    t.name = uniqueTrackName(doc, name.value_or(trackKindDefaultName(kind)));
    t.kind = kind;
    t.format = format;
    t.color = (kind == TrackKind::Master) ? std::array<uint8_t, 3>{140, 140, 140} : nextTrackColor(doc);
    if (format == ChannelFormat::Mono) {
        t.mixer.pan = {0.0f};
    } else {
        t.mixer.pan = {-1.0f, 1.0f};
    }
    size_t pos = doc.tracks.size();
    if (kind != TrackKind::Master) {
        for (size_t i = 0; i < doc.tracks.size(); ++i) {
            if (doc.tracks[i].kind == TrackKind::Master) {
                pos = i;
                break;
            }
        }
    }
    doc.tracks.insert(doc.tracks.begin() + static_cast<std::ptrdiff_t>(pos), std::move(t));
    return doc.tracks[pos].numId;
}

Source* findSourceByNum(Document& doc, uint64_t numId) {
    for (auto& s : doc.sources) {
        if (s.numId == numId || s.id == "src_" + std::to_string(numId)) return &s;
    }
    return nullptr;
}

const Source* findSourceByNum(const Document& doc, uint64_t numId) {
    for (const auto& s : doc.sources) {
        if (s.numId == numId || s.id == "src_" + std::to_string(numId)) return &s;
    }
    return nullptr;
}

// after SoundCraft (crates/model/src/session.rs Session::clips_using)
std::vector<const Clip*> clipsUsing(const Document& doc, uint64_t sourceNumId) {
    std::vector<const Clip*> out;
    const Source* src = findSourceByNum(doc, sourceNumId);
    const std::string srcId = src ? src->id : ("src_" + std::to_string(sourceNumId));
    auto matches = [&](const Clip& c) {
        if (c.isMidi) return false;
        return c.sourceId == srcId || (src && !src->name.empty() && c.sourceId == src->name) ||
               c.sourceId == std::to_string(sourceNumId);
    };
    for (const auto& t : doc.tracks) {
        for (const auto& c : t.clips) {
            if (matches(c)) out.push_back(&c);
        }
        for (const auto& pl : t.playlists) {
            for (const auto& c : pl.clips) {
                if (matches(c)) out.push_back(&c);
            }
        }
    }
    return out;
}

} // namespace Session