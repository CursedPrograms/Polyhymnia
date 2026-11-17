// Polyhymnia - editing commands on the session model.
#include "editor.h"
#include "tempo.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace Editor {

static Mode gMode = Mode::Slip;
static Tool gTool = Tool::Smart;
static bool gSnap = true;
static Selection gSel;
static Session::Document gClipboard;
static uint32_t gIdSeq = 1000;

static std::string nextId(const char* prefix) { return std::string(prefix) + std::to_string(gIdSeq++); }

static Session::Track* trackMut(Session::Document& doc, const std::string& id) { return Session::findTrack(doc, id); }

static Session::Clip* clipMut(Session::Track& t, const std::string& id) {
    for (auto& c : t.clips)
        if (c.id == id) return &c;
    return nullptr;
}

static void sortClips(Session::Track& t) {
    std::sort(t.clips.begin(), t.clips.end(), [](const Session::Clip& a, const Session::Clip& b) { return a.start < b.start; });
}

static void bumpTouch(Session::Document& doc) { ++doc.touch; }

Mode mode() { return gMode; }
Tool tool() { return gTool; }
void setMode(Mode m) { gMode = m; }
void setTool(Tool t) { gTool = t; }

const char* modeLabel(Mode m) {
    switch (m) {
    case Mode::Shuffle: return "Shuffle";
    case Mode::Slip: return "Slip";
    case Mode::Spot: return "Spot";
    case Mode::Grid: return "Grid";
    case Mode::GridRelative: return "Rel grid";
    }
    return "Slip";
}

const char* toolLabel(Tool t) {
    switch (t) {
    case Tool::Smart: return "Smart";
    case Tool::Selector: return "Selector";
    case Tool::Grabber: return "Grabber";
    case Tool::Trim: return "Trim";
    }
    return "Smart";
}

Selection& selection() { return gSel; }

bool snapToGrid() { return gSnap; }
void setSnapToGrid(bool on) { gSnap = on; }

long long gridStepSamples(const Session::Document& doc) {
    const double spb = doc.time.samplesPerBeat(doc.sampleRate);
    if (gMode == Mode::GridRelative) return std::max(1LL, (long long)(spb / 4.0));
    return std::max(1LL, (long long)spb);
}

long long snapSample(long long s, const Session::Document& doc) {
    if (!gSnap || !(gMode == Mode::Grid || gMode == Mode::GridRelative)) return s;
    const long long step = gridStepSamples(doc);
    return ((s + step / 2) / step) * step;
}

void selectClip(const Session::Document& doc, const std::string& trackId, const std::string& clipId) {
    gSel.trackId = trackId;
    gSel.clips = {{trackId, clipId}};
    gSel.start = 0;
    gSel.end = 0;
    for (const auto& t : doc.tracks) {
        if (t.id != trackId) continue;
        for (const auto& c : t.clips)
            if (c.id == clipId) {
                gSel.start = c.start;
                gSel.end = c.start + c.length;
                return;
            }
    }
}

void selectRange(long long start, long long end, const std::string& trackId) {
    gSel.start = std::min(start, end);
    gSel.end = std::max(start, end);
    gSel.trackId = trackId;
    gSel.clips.clear();
}

void clearSelection() {
    gSel = {};
}

bool hasClipboard() { return !gClipboard.tracks.empty(); }
void clearClipboard() { gClipboard = Session::empty(); }

static std::vector<Session::Clip> clipsInRange(const Session::Document& doc) {
    std::vector<Session::Clip> out;
    for (const auto& t : doc.tracks) {
        if (!gSel.trackId.empty() && t.id != gSel.trackId) continue;
        for (const auto& c : t.clips) {
            const long long c0 = c.start;
            const long long c1 = c.start + c.length;
            if (c1 > gSel.start && c0 < gSel.end) out.push_back(c);
        }
    }
    if (!gSel.clips.empty()) {
        out.clear();
        for (const auto& r : gSel.clips) {
            const Session::Track* t = Session::findTrack(doc, r.trackId);
            if (!t) continue;
            for (const auto& c : t->clips)
                if (c.id == r.clipId) out.push_back(c);
        }
    }
    return out;
}

bool copy(const Session::Document& doc) {
    auto clips = clipsInRange(doc);
    if (clips.empty() && gSel.clips.empty()) return false;
    if (clips.empty()) return false;
    gClipboard = Session::empty();
    gClipboard.sources = doc.sources;
    Session::Track tr;
    tr.id = "clipclip";
    tr.name = "Clipboard";
    tr.clips = std::move(clips);
    const long long base = tr.clips[0].start;
    for (auto& c : tr.clips) {
        c.start -= base;
        c.id = nextId("cb");
    }
    gClipboard.tracks.push_back(std::move(tr));
    return true;
}

bool cut(Session::Document& doc) {
    if (!copy(doc)) return false;
    auto clips = clipsInRange(doc);
    for (const auto& c : clips) {
        Session::Track* t = trackMut(doc, gSel.trackId.empty() ? doc.tracks[0].id : gSel.trackId);
        if (!t) continue;
        t->clips.erase(std::remove_if(t->clips.begin(), t->clips.end(), [&](const Session::Clip& x) { return x.id == c.id; }), t->clips.end());
        if (gMode == Mode::Shuffle) {
            const long long hole = c.length;
            for (auto& o : t->clips)
                if (o.start > c.start) o.start -= hole;
        }
    }
    bumpTouch(doc);
    return true;
}

bool paste(Session::Document& doc, long long at, const std::string& trackId) {
    if (!hasClipboard()) return false;
    Session::Track* t = trackMut(doc, trackId);
    if (!t) return false;
    const auto& src = gClipboard.tracks[0].clips;
    const long long pasteAt = snapSample(at, doc);
    for (const auto& c : src) {
        Session::Clip n = c;
        n.id = nextId("c");
        n.start = pasteAt + c.start;
        if (gMode == Mode::Shuffle) {
            const long long need = n.start + n.length;
            for (auto& o : t->clips)
                if (o.start >= n.start) o.start += n.length;
        }
        t->clips.push_back(n);
    }
    sortClips(*t);
    bumpTouch(doc);
    return true;
}

bool duplicateSelection(Session::Document& doc) {
    if (!copy(doc)) return false;
    const long long at = gSel.end > gSel.start ? gSel.end : gSel.start;
    return paste(doc, at, gSel.trackId.empty() && !doc.tracks.empty() ? doc.tracks[0].id : gSel.trackId);
}

bool separateAtPlayhead(Session::Document& doc, long long at) {
    const long long pos = snapSample(at, doc);
    int splits = 0;
    for (auto& t : doc.tracks) {
        if (!gSel.trackId.empty() && t.id != gSel.trackId) continue;
        std::vector<Session::Clip> out;
        for (auto c : t.clips) {
            if (pos <= c.start || pos >= c.start + c.length) {
                out.push_back(c);
                continue;
            }
            Session::Clip right = c;
            right.id = nextId("c");
            const long long leftLen = pos - c.start;
            right.offset += leftLen;
            right.length -= leftLen;
            right.start = pos;
            right.fadeIn = {};
            c.length = leftLen;
            c.fadeOut = {};
            out.push_back(c);
            out.push_back(right);
            ++splits;
        }
        t.clips = std::move(out);
        sortClips(t);
    }
    if (splits > 0) bumpTouch(doc);
    return splits > 0;
}

bool healSeparation(Session::Document& doc) {
    int healed = 0;
    for (auto& t : doc.tracks) {
        sortClips(t);
        for (size_t i = 0; i + 1 < t.clips.size(); ++i) {
            Session::Clip& a = t.clips[i];
            Session::Clip& b = t.clips[i + 1];
            if (a.sourceId != b.sourceId) continue;
            if (a.start + a.length != b.start) continue;
            if (a.fadeOut.len > 0 || b.fadeIn.len > 0) continue;
            a.length += b.length;
            a.fadeOut = b.fadeOut;
            t.clips.erase(t.clips.begin() + (ptrdiff_t)i + 1);
            ++healed;
            --i;
        }
    }
    if (healed > 0) bumpTouch(doc);
    return healed > 0;
}

bool consolidateSelection(Session::Document& doc) {
    Session::Track* t = trackMut(doc, gSel.trackId);
    if (!t || gSel.clips.size() != 1) return false;
    Session::Clip* c = clipMut(*t, gSel.clips[0].clipId);
    if (!c) return false;
    const long long oldLen = c->length;
    c->length = std::max(c->length, gSel.end - gSel.start);
    if (c->length != oldLen) bumpTouch(doc);
    return c->length != oldLen;
}

bool nudgeSelection(Session::Document& doc, long long delta) {
    if (gSel.clips.empty()) return false;
    bool moved = false;
    for (const auto& r : gSel.clips) {
        Session::Track* t = trackMut(doc, r.trackId);
        Session::Clip* c = t ? clipMut(*t, r.clipId) : nullptr;
        if (!c) continue;
        if (gMode == Mode::Slip || gMode == Mode::Grid || gMode == Mode::GridRelative) {
            c->start = snapSample(c->start + delta, doc);
            moved = true;
        } else if (gMode == Mode::Shuffle) {
            moved |= moveClip(doc, r.trackId, r.clipId, snapSample(c->start + delta, doc));
        } else if (gMode == Mode::Spot) {
            c->offset = std::max(0LL, c->offset + delta);
            moved = true;
        }
    }
    if (moved) bumpTouch(doc);
    return moved;
}

static void rippleShuffle(Session::Track& t, const std::string& skipId, long long from, long long delta) {
    for (auto& c : t.clips) {
        if (c.id == skipId) continue;
        if (c.start >= from) c.start += delta;
    }
}

bool moveClip(Session::Document& doc, const std::string& trackId, const std::string& clipId, long long newStart) {
    Session::Track* t = trackMut(doc, trackId);
    Session::Clip* c = t ? clipMut(*t, clipId) : nullptr;
    if (!c) return false;
    const long long target = snapSample(newStart, doc);
    if (gMode == Mode::Shuffle) {
        const long long old = c->start;
        const long long delta = target - old;
        c->start = target;
        rippleShuffle(*t, clipId, old + c->length, delta);
    } else if (gMode == Mode::Spot) {
        c->start = target;
    } else {
        c->start = target;
    }
    sortClips(*t);
    bumpTouch(doc);
    return true;
}

bool trimClipStart(Session::Document& doc, const std::string& trackId, const std::string& clipId, long long newStart) {
    Session::Track* t = trackMut(doc, trackId);
    Session::Clip* c = t ? clipMut(*t, clipId) : nullptr;
    if (!c) return false;
    const long long ns = snapSample(newStart, doc);
    if (ns <= c->start || ns >= c->start + c->length) return false;
    const long long delta = ns - c->start;
    c->offset += delta;
    c->length -= delta;
    c->start = ns;
    if (gMode == Mode::Shuffle) rippleShuffle(*t, clipId, ns, -delta);
    bumpTouch(doc);
    return true;
}

bool trimClipEnd(Session::Document& doc, const std::string& trackId, const std::string& clipId, long long newEnd) {
    Session::Track* t = trackMut(doc, trackId);
    Session::Clip* c = t ? clipMut(*t, clipId) : nullptr;
    if (!c) return false;
    const long long ne = snapSample(newEnd, doc);
    if (ne <= c->start || ne > c->start + c->length) return false;
    c->length = ne - c->start;
    bumpTouch(doc);
    return true;
}

void setClipGainDb(Session::Document& doc, const std::string& trackId, const std::string& clipId, float gainDb) {
    Session::Track* t = trackMut(doc, trackId);
    Session::Clip* c = t ? clipMut(*t, clipId) : nullptr;
    if (!c) return;
    c->gainDb = gainDb;
    bumpTouch(doc);
}

void setClipFade(Session::Document& doc, const std::string& trackId, const std::string& clipId, bool fadeIn, long long len,
                 Session::FadeShape shape) {
    Session::Track* t = trackMut(doc, trackId);
    Session::Clip* c = t ? clipMut(*t, clipId) : nullptr;
    if (!c) return;
    Session::Fade f;
    f.len = std::max(0LL, len);
    f.shape = shape;
    if (fadeIn)
        c->fadeIn = f;
    else
        c->fadeOut = f;
    bumpTouch(doc);
}

void applyCrossfadeOverlap(Session::Document& doc, const std::string& trackId, const std::string& clipA, const std::string& clipB,
                           long long overlap) {
    Session::Track* t = trackMut(doc, trackId);
    Session::Clip* a = t ? clipMut(*t, clipA) : nullptr;
    Session::Clip* b = t ? clipMut(*t, clipB) : nullptr;
    if (!a || !b || overlap <= 0) return;
    a->fadeOut = {overlap, Session::FadeShape::EqualPower};
    b->fadeIn = {overlap, Session::FadeShape::EqualPower};
    bumpTouch(doc);
}

void scriptDemo(Session::Document& doc) {
    setMode(Mode::Grid);
    setTool(Tool::Smart);
    setSnapToGrid(true);
    if (doc.tracks.empty()) return;
    const std::string tid = doc.tracks[0].id;
    if (doc.tracks[0].clips.empty()) return;
    const auto& c0 = doc.tracks[0].clips[0];
    selectClip(doc, tid, c0.id);
    separateAtPlayhead(doc, c0.start + c0.length / 2);
    if (doc.tracks[0].clips.size() >= 2) {
        setClipFade(doc, tid, doc.tracks[0].clips[0].id, false, 2400, Session::FadeShape::EqualPower);
        setClipFade(doc, tid, doc.tracks[0].clips[1].id, true, 2400, Session::FadeShape::SCurve);
        applyCrossfadeOverlap(doc, tid, doc.tracks[0].clips[0].id, doc.tracks[0].clips[1].id, 1200);
    }
    setClipGainDb(doc, tid, doc.tracks[0].clips[0].id, -6.0f);
    duplicateSelection(doc);
}

} // namespace Editor