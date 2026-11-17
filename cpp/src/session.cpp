// Polyhymnia - in-memory session; see session.h.
#include "session.h"
#include "clip_ops.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Session {

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
    sine.id = "sine";
    sine.path = "audio/sine_440.wav";
    sine.sampleRate = 48000;
    sine.channels = 1;
    sine.frames = 48000 * 4;
    d.sources.push_back(sine);
    Session::Source click;
    click.id = "click";
    click.path = "audio/click.wav";
    click.sampleRate = 48000;
    click.channels = 1;
    click.frames = 48000;
    d.sources.push_back(click);
    Track a;
    a.id = "t1";
    a.name = "Lead";
    Session::Clip c1;
    c1.id = "c1";
    c1.name = "Lead clip";
    c1.sourceId = "sine";
    c1.start = 0;
    c1.length = 48000 * 2;
    c1.gainDb = -3.0f;
    a.clips.push_back(c1);
    d.tracks.push_back(a);
    Track b;
    b.id = "t2";
    b.name = "Click";
    b.gainDb = -6.0f;
    Session::Clip c2;
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
    if (a.tracks.size() != b.tracks.size()) return false;
    if (a.markers.size() != b.markers.size()) return false;
    for (size_t i = 0; i < a.tracks.size(); ++i) {
        const auto& ta = a.tracks[i];
        const auto& tb = b.tracks[i];
        if (ta.clips.size() != tb.clips.size() || ta.playlists.size() != tb.playlists.size()) return false;
        for (size_t j = 0; j < ta.clips.size(); ++j)
            if (!clipsEqual(ta.clips[j], tb.clips[j])) return false;
    }
    return true;
}

static Source addMonoSource(Document& doc, const std::string& name, std::vector<float> pcm, int sr) {
    Source s;
    s.numId = doc.nextSourceId++;
    s.id = "src_" + std::to_string(s.numId);
    s.path = name;
    s.sampleRate = sr;
    s.channels = 1;
    s.frames = static_cast<long long>(pcm.size());
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
    tk.clips.push_back(makeAudioClip(d, kickSrc.id, kickSrc.numId, static_cast<long long>(bar * 4), static_cast<long long>(bar * 4),
                                     static_cast<long long>(total - bar * 4), "Kick"));
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
    t.id = "tr_" + std::to_string(doc.tracks.size() + 1);
    t.name = name ? name : "Audio";
    t.kind = TrackKind::Audio;
    doc.tracks.push_back(std::move(t));
    return doc.tracks.back().id;
}

} // namespace Session