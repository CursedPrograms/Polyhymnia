// Polyhymnia - editing commands on the session model (after SoundCraft crates/engine edit + cmd/edit.rs).
#include "editor.h"
#include "clip_ops.h"
#include "consolidate.h"
#include "engine_edit.h"
#include "time_grid.h"
#include "tempo.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace Editor {

static Mode gMode = Mode::Slip;
static Tool gTool = Tool::Smart;
static bool gSnap = true;
static Selection gSel;
static Session::Document gClipboard;

static void bumpTouch(Session::Document& doc) { ++doc.touch; }

static bool shuffleEdit() { return gMode == Mode::Shuffle; }

static Time::Range selectionRange(const Session::Document& doc) {
    if (gSel.end > gSel.start) return Time::Range::newRange(gSel.start, gSel.end);
    if (gSel.clips.empty()) return {};
    long long lo = std::numeric_limits<long long>::max();
    long long hi = std::numeric_limits<long long>::min();
    for (const auto& r : gSel.clips) {
        const Session::Track* t = Session::findTrack(doc, r.trackId);
        if (!t) continue;
        for (const auto& c : t->clips) {
            if (c.id != r.clipId) continue;
            lo = std::min(lo, c.start);
            hi = std::max(hi, ClipOps::end(c));
        }
    }
    if (lo > hi) return {};
    return Time::Range::newRange(lo, hi);
}

static std::vector<std::string> tracksInScope(const Session::Document& doc) {
    if (!gSel.trackId.empty()) return {gSel.trackId};
    const Time::Range r = selectionRange(doc);
    if (r.isEmpty()) return {};
    std::vector<std::string> out;
    for (const auto& t : doc.tracks) {
        for (const auto& c : t.clips) {
            if (ClipOps::range(c).overlaps(r)) {
                out.push_back(t.id);
                break;
            }
        }
    }
    return out;
}

static std::vector<std::string> clipIdsFromSelection(const Session::Document& doc) {
    if (!gSel.clips.empty()) {
        std::vector<std::string> ids;
        ids.reserve(gSel.clips.size());
        for (const auto& r : gSel.clips) ids.push_back(r.clipId);
        return ids;
    }
    const Time::Range r = selectionRange(doc);
    if (r.isEmpty()) return {};
    std::vector<std::string> ids;
    for (const auto& tid : tracksInScope(doc)) {
        const Session::Track* t = Session::findTrack(doc, tid);
        if (!t) continue;
        for (const auto& c : t->clips)
            if (ClipOps::range(c).overlaps(r)) ids.push_back(c.id);
    }
    return ids;
}

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
    if (gMode == Mode::GridRelative) {
        const long long step = TimeGrid::stepSamples(doc);
        return std::max(1LL, step / 4);
    }
    return TimeGrid::stepSamples(doc);
}

long long snapSample(long long s, const Session::Document& doc) {
    if (!gSnap || !(gMode == Mode::Grid || gMode == Mode::GridRelative)) return s;
    const long long step = gridStepSamples(doc);
    return ((s + step / 2) / step) * step;
}

std::vector<std::string> selectedClipIds(const Session::Document& doc) { return clipIdsFromSelection(doc); }

void selectClips(const Session::Document& doc, const std::vector<std::string>& clipIds, const std::string& trackId) {
    gSel.trackId = trackId;
    gSel.clips.clear();
    gSel.clips.reserve(clipIds.size());
    for (const auto& id : clipIds) gSel.clips.push_back({trackId, id});
    gSel.start = std::numeric_limits<long long>::max();
    gSel.end = std::numeric_limits<long long>::min();
    for (const auto& id : clipIds) {
        if (auto fc = Session::findClip(doc, id)) {
            gSel.start = std::min(gSel.start, fc->second->start);
            gSel.end = std::max(gSel.end, ClipOps::end(*fc->second));
        }
    }
    if (gSel.start > gSel.end) {
        gSel.start = 0;
        gSel.end = 0;
    }
}

void addClipToSelection(const Session::Document& doc, const std::string& trackId, const std::string& clipId) {
    for (const auto& r : gSel.clips)
        if (r.clipId == clipId) return;
    gSel.clips.push_back({trackId, clipId});
    gSel.trackId = trackId;
    if (auto fc = Session::findClip(doc, clipId)) {
        gSel.start = std::min(gSel.start == 0 && gSel.end == 0 ? fc->second->start : gSel.start, fc->second->start);
        gSel.end = std::max(gSel.end, ClipOps::end(*fc->second));
    }
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
                gSel.end = ClipOps::end(c);
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

void clearSelection() { gSel = {}; }

bool hasClipboard() { return !gClipboard.tracks.empty(); }
void clearClipboard() { gClipboard = Session::empty(); }

bool copy(const Session::Document& doc) {
    const Time::Range r = selectionRange(doc);
    if (r.isEmpty()) return false;
    gClipboard = Session::empty();
    gClipboard.sources = doc.sources;
    for (const auto& tid : tracksInScope(doc)) {
        const auto clips = EngEdit::copyRange(doc, tid, r);
        if (clips.empty()) continue;
        Session::Track tr;
        tr.id = tid;
        tr.clips = clips;
        gClipboard.tracks.push_back(std::move(tr));
    }
    return !gClipboard.tracks.empty();
}

bool cut(Session::Document& doc) {
    if (!copy(doc)) return false;
    const Time::Range r = selectionRange(doc);
    const bool sh = shuffleEdit();
    for (const auto& tid : tracksInScope(doc)) EngEdit::clearRange(doc, tid, r, sh);
    bumpTouch(doc);
    return true;
}

bool paste(Session::Document& doc, long long at, const std::string& trackId) {
    if (!hasClipboard()) return false;
    Session::Track* t = Session::findTrack(doc, trackId);
    if (!t) return false;
    long long length = 0;
    for (const auto& tr : gClipboard.tracks) {
        for (const auto& c : tr.clips) length = std::max(length, ClipOps::end(c));
    }
    const long long pasteAt =
        (gMode == Mode::Grid || gMode == Mode::GridRelative) ? snapSample(at, doc) : at;
    const auto& clips = gClipboard.tracks[0].clips;
    const auto out = EngEdit::paste(doc, trackId, pasteAt, clips, length, shuffleEdit());
    if (out.error) return false;
    for (const auto& s : gClipboard.sources) {
        bool have = false;
        for (const auto& e : doc.sources)
            if (e.id == s.id) {
                have = true;
                break;
            }
        if (!have) doc.sources.push_back(s);
    }
    bumpTouch(doc);
    return !out.ids.empty();
}

bool duplicateSelection(Session::Document& doc) {
    if (!copy(doc)) return false;
    const Time::Range r = selectionRange(doc);
    const long long at = r.isEmpty() ? gSel.start : r.end;
    std::string tid = gSel.trackId;
    if (tid.empty() && !doc.tracks.empty()) tid = doc.tracks[0].id;
    return paste(doc, at, tid);
}

bool separateAtPlayhead(Session::Document& doc, long long at) {
    const long long pos = (gMode == Mode::Grid || gMode == Mode::GridRelative) ? snapSample(at, doc) : at;
    size_t splits = 0;
    const auto tracks = tracksInScope(doc);
    if (tracks.empty()) {
        for (const auto& t : doc.tracks) splits += EngEdit::separateAt(doc, t.id, pos);
    } else {
        for (const auto& tid : tracks) splits += EngEdit::separateAt(doc, tid, pos);
    }
    if (splits > 0) bumpTouch(doc);
    return splits > 0;
}

bool healSeparation(Session::Document& doc) {
    const Time::Range r = selectionRange(doc);
    if (r.isEmpty()) return false;
    size_t healed = 0;
    for (const auto& tid : tracksInScope(doc)) healed += EngEdit::heal(doc, tid, r);
    if (healed > 0) bumpTouch(doc);
    return healed > 0;
}

bool consolidateSelection(Session::Document& doc) {
    const Time::Range r = selectionRange(doc);
    if (r.isEmpty()) return false;
    bool any = false;
    std::string err;
    for (const auto& tid : tracksInScope(doc)) {
        Session::Track* t = Session::findTrack(doc, tid);
        if (!t) continue;
        if (Session::isMidiKind(t->kind)) {
            if (EngEdit::consolidateMidi(doc, tid, r)) any = true;
        } else if (Consolidate::track(doc, tid, r, err)) {
            any = true;
        }
    }
    if (any) bumpTouch(doc);
    return any;
}

bool nudgeSelection(Session::Document& doc, long long delta) {
    if (delta == 0) return false;
    const std::vector<std::string> ids = clipIdsFromSelection(doc);
    if (ids.empty()) return false;
    if (gMode == Mode::Spot) return false;

    const long long step = delta;

    if (gMode == Mode::Slip) {
        bool moved = false;
        for (const auto& id : ids) {
            auto fc = Session::findClip(doc, id);
            if (!fc) continue;
            Session::Clip* c = fc->second;
            c->offset = std::max(0LL, Time::satAdd(c->offset, step));
            moved = true;
        }
        if (moved) bumpTouch(doc);
        return moved;
    }

    long long effective = step;
    if (gMode == Mode::Grid || gMode == Mode::GridRelative) {
        if (const Session::Clip* c = EngEdit::firstMovable(doc, ids))
            effective = EngEdit::gridDelta(doc, *c, step, gMode == Mode::GridRelative, ids);
    }

    size_t moved = 0;
    if (gMode == Mode::Shuffle) {
        const auto out = EngEdit::shuffleClips(doc, ids, effective, {});
        if (out.error) return false;
        moved = out.count;
    } else {
        moved = EngEdit::moveClips(doc, ids, effective, {});
    }
    if (moved > 0) {
        gSel.start = Time::satAdd(gSel.start, effective);
        gSel.end = Time::satAdd(gSel.end, effective);
        if (gSel.start < 0) {
            const long long fix = -gSel.start;
            gSel.start += fix;
            gSel.end += fix;
        }
        bumpTouch(doc);
    }
    return moved > 0;
}

bool moveClip(Session::Document& doc, const std::string& trackId, const std::string& clipId, long long newStart) {
    auto fc = Session::findClip(doc, clipId);
    if (!fc || fc->first != trackId) return false;
    Session::Clip& c = *fc->second;
    const long long old = c.start;
    long long target = newStart;
    if (gMode == Mode::Grid || gMode == Mode::GridRelative) target = snapSample(newStart, doc);
    long long delta = target - old;
    if (gMode == Mode::Grid || gMode == Mode::GridRelative) {
        const std::vector<std::string> ids = {clipId};
        delta = EngEdit::gridDelta(doc, c, delta, gMode == Mode::GridRelative, ids);
    }
    if (gMode == Mode::Shuffle) {
        const auto out = EngEdit::shuffleClips(doc, {clipId}, delta, {});
        if (out.error || out.count == 0) return false;
        bumpTouch(doc);
        return true;
    }
    if (EngEdit::moveClips(doc, {clipId}, delta, {}) == 0) return false;
    bumpTouch(doc);
    return true;
}

bool trimClipStart(Session::Document& doc, const std::string& trackId, const std::string& clipId, long long newStart) {
    (void)trackId;
    const long long ns = (gMode == Mode::Grid || gMode == Mode::GridRelative) ? snapSample(newStart, doc) : newStart;
    if (!EngEdit::trimClip(doc, clipId, ns, std::nullopt, shuffleEdit())) return false;
    bumpTouch(doc);
    return true;
}

bool trimClipEnd(Session::Document& doc, const std::string& trackId, const std::string& clipId, long long newEnd) {
    (void)trackId;
    const long long ne = (gMode == Mode::Grid || gMode == Mode::GridRelative) ? snapSample(newEnd, doc) : newEnd;
    if (!EngEdit::trimClip(doc, clipId, std::nullopt, ne, shuffleEdit())) return false;
    bumpTouch(doc);
    return true;
}

void setClipGainDb(Session::Document& doc, const std::string& trackId, const std::string& clipId, float gainDb) {
    Session::Clip* c = Session::clipMut(doc, trackId, clipId);
    if (!c) return;
    c->gainDb = gainDb;
    bumpTouch(doc);
}

void setClipFade(Session::Document& doc, const std::string& trackId, const std::string& clipId, bool fadeIn, long long len,
                 Session::FadeShape shape) {
    Session::Clip* c = Session::clipMut(doc, trackId, clipId);
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
    Session::Clip* a = Session::clipMut(doc, trackId, clipA);
    Session::Clip* b = Session::clipMut(doc, trackId, clipB);
    if (!a || !b || overlap <= 0) return;
    const long long lo = ClipOps::end(*a) - overlap;
    const long long hi = b->start + overlap;
    if (EngEdit::createFades(doc, trackId, Time::Range::newRange(lo, hi), Session::FadeShape::EqualPower) > 0) bumpTouch(doc);
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