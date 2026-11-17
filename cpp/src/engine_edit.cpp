// Editing algorithms on playlists (after SoundCraft crates/engine/src/edit.rs).
#include "engine_edit.h"
#include "clip_ops.h"
#include "time_grid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace EngEdit {

const char* SNAP_TO_GRID = "edit.snap_to_grid";
const char* SHUFFLE_LOCK = "edit.shuffle_lock";
const char* RELATIVE_GRID = "edit.relative_grid";
const char* NO_ROOM = "not enough room before a locked clip";
const char* IN_CROSSFADE = "can't make room inside a crossfade";

constexpr long long kSamplesMax = std::numeric_limits<long long>::max();

namespace {

struct Span {
    std::string id;
    long long start = 0;
    long long end = 0;
    bool locked = false;
};

struct Lift {
    std::string id;
    long long start = 0;
    long long from = 0;
    long long shift = 0;
    long long by = 0;
    long long lock = 0;
};

using AutoPoint = std::pair<Session::AutoParam, Session::AutomationPoint>;

Session::Track* playlistMut(Session::Document& s, const std::string& trackId) { return Session::findTrack(s, trackId); }

const Session::Track* playlist(const Session::Document& s, const std::string& trackId) { return Session::findTrack(s, trackId); }

// after SoundCraft (crates/engine/src/edit.rs rebase_midi_start)
void rebaseMidiStart(Session::Clip&, long long, const TimeCtx&) {}

// after SoundCraft (crates/engine/src/edit.rs take_auto_name)
void takeAutoName(std::map<uint64_t, Session::AutoRenameScheme>* schemes, Session::Clip* c) {
    if (!c->autoNameScheme || !schemes) return;
    const auto it = schemes->find(*c->autoNameScheme);
    if (it == schemes->end()) return;
    c->name = it->second.pattern + " " + std::to_string(it->second.number);
    it->second.number = it->second.number + 1;
}

std::vector<Span> spans(const std::vector<Session::Clip>& clips) {
    std::vector<Span> out;
    out.reserve(clips.size());
    for (const auto& c : clips) out.push_back({c.id, c.start, ClipOps::end(c), ClipOps::locked(c)});
    return out;
}

// after SoundCraft (crates/engine/src/edit.rs ripple_spans)
std::pair<long long, long long> rippleSpans(std::vector<Span>* sp, long long from, long long by) {
    long long lock = kSamplesMax;
    for (const auto& c : *sp) {
        if (c.locked && c.start >= from) lock = std::min(lock, c.start);
    }
    auto moves = [&](const Span& c) { return c.start >= from && c.start < lock; };
    long long floor = 0;
    bool any = false;
    for (const auto& c : *sp) {
        if (moves(c)) {
            floor = any ? std::min(floor, c.start) : c.start;
            any = true;
        }
    }
    by = std::max(by, -floor);
    for (auto& c : *sp) {
        if (moves(c)) {
            c.start = Time::satAdd(c.start, by);
            c.end = Time::satAdd(c.end, by);
        }
    }
    return {by, lock};
}

// after SoundCraft (crates/engine/src/edit.rs join_gain_env)
void joinGainEnv(Session::Clip* l, const Session::Clip& r) {
    const long long cut = l->length;
    const long long seam = Time::satAdd(r.start, -l->start);
    const long long last = Time::satSub(cut, 1);
    const float endDb = ClipOps::envDbAt(*l, last);
    const float startDb = ClipOps::envDbAt(r, 0);
    l->gainEnv.erase(std::remove_if(l->gainEnv.begin(), l->gainEnv.end(), [&](const auto& p) { return p.first >= cut; }), l->gainEnv.end());
    for (const auto& p : r.gainEnv) {
        if (p.first >= 0) {
            const long long at = Time::satAdd(p.first, seam);
            l->gainEnv.push_back({at, p.second});
        }
    }
  for (long long at : {seam, last}) {
        const float db = (at == seam) ? startDb : endDb;
        if (std::abs(ClipOps::envDbAt(*l, at) - db) > 1e-4f) {
            const size_t i = std::partition_point(l->gainEnv.begin(), l->gainEnv.end(), [&](const auto& p) { return p.first < at; }) -
                             l->gainEnv.begin();
            l->gainEnv.insert(l->gainEnv.begin() + (ptrdiff_t)i, {at, db});
        }
    }
}

// after SoundCraft (crates/engine/src/edit.rs source_len)
long long sourceLen(long long length, double stretch) {
    if (std::isfinite(stretch) && stretch > 0.0 && std::abs(stretch - 1.0) >= 1e-9) return (long long)std::llround(double(length) / stretch);
    return length;
}

// after SoundCraft (crates/engine/src/edit.rs material_len)
std::optional<long long> materialLen(const Session::Document& s, const Session::Clip& c) {
    if (c.isMidi) return std::nullopt;
    return Session::sourceFrames(s, c.sourceId);
}

std::pair<long long, long long> ripple(Session::Document& s, const std::string& track, long long from, long long by) {
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return {0, kSamplesMax};
    auto sp = spans(pl->clips);
    const auto moved = rippleSpans(&sp, from, by);
    for (size_t i = 0; i < pl->clips.size(); ++i) pl->clips[i].start = sp[i].start;
    Session::sortPlaylist(*pl);
    return moved;
}

void followAutomation(Session::Document& s, const std::string& track, long long from, long long by, long long lock) {
    if (!s.edit.automationFollowsEdit || by == 0) return;
    Session::Track* t = playlistMut(s, track);
    if (!t) return;
    for (auto& l : t->automation) {
        if (by < 0) l.clearRange(Time::satAdd(from, by), from);
        for (auto& p : l.points) {
            if (p.at >= from && p.at < lock) p.at = Time::satAdd(p.at, by);
        }
        std::sort(l.points.begin(), l.points.end(), [](const Session::AutomationPoint& a, const Session::AutomationPoint& b) { return a.at < b.at; });
        l.points.erase(std::unique(l.points.begin(), l.points.end(), [](const Session::AutomationPoint& a, const Session::AutomationPoint& b) {
                                 return a.at == b.at;
                             }),
                         l.points.end());
    }
}

std::vector<AutoPoint> takeAutomation(Session::Document& s, const std::string& track, long long start, long long end, long long origin) {
    std::vector<AutoPoint> out;
    if (!s.edit.automationFollowsEdit || end <= start) return out;
    Session::Track* t = playlistMut(s, track);
    if (!t) return out;
    for (auto& l : t->automation) {
        for (const auto& p : l.points) {
            if (p.at >= start && p.at < end)
                out.push_back({l.param, {Time::satAdd(p.at, -origin), p.value}});
        }
        l.clearRange(start, end);
    }
    return out;
}

void putAutomation(Session::Document& s, const std::string& track, long long origin, const std::vector<AutoPoint>& points) {
    Session::Track* t = playlistMut(s, track);
    if (!t) return;
    for (const auto& pr : points) t->laneMut(pr.first)->setPoint(Time::satAdd(origin, pr.second.at), pr.second.value);
}

std::pair<std::vector<Lift>, std::vector<Span>> liftPlan(std::vector<Span> sp, const std::unordered_set<std::string>& moving) {
    std::sort(sp.begin(), sp.end(), [](const Span& a, const Span& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.id < b.id;
    });
    auto lifted = [&](const Span& c) { return !c.locked && moving.count(c.id); };
    std::vector<long long> reach;
    reach.reserve(sp.size() + 1);
    long long far = std::numeric_limits<long long>::min();
    reach.push_back(far);
    for (const auto& c : sp) {
        far = std::max(far, c.end);
        reach.push_back(far);
    }
    std::vector<Lift> lifts;
    std::optional<long long> next;
    bool nextMoves = false;
    long long lock = kSamplesMax;
    for (ptrdiff_t i = (ptrdiff_t)sp.size() - 1; i >= 0; --i) {
        const Span& c = sp[(size_t)i];
        if (!lifted(c)) {
            next = c.start;
            nextMoves = !c.locked;
            if (c.locked) lock = c.start;
            continue;
        }
        const long long from = std::max(c.start, reach[(size_t)i]);
        const long long cap = next.value_or(c.end);
        const long long shift = std::max(Time::satAdd(std::min(cap, c.end), -from), 0LL);
        long long by = nextMoves ? -std::min(shift, std::max(next.value_or(0LL), 0LL)) : 0;
        if (next) next = Time::satAdd(*next, by);
        lifts.push_back({c.id, c.start, from, shift, by, lock});
    }
    long long accBy = 0;
    auto pending = lifts.rbegin();
    const auto pendingEnd = lifts.rend();
    std::vector<Span> staying;
    staying.reserve(sp.size());
    for (const auto& c : sp) {
        if (lifted(c)) {
            if (pending != pendingEnd) {
                accBy = Time::satAdd(accBy, pending->by);
                ++pending;
            }
        } else if (c.locked) {
            accBy = 0;
            staying.push_back(c);
        } else {
            staying.push_back({c.id, Time::satAdd(c.start, accBy), Time::satAdd(c.end, accBy), c.locked});
        }
    }
    return {lifts, staying};
}

std::vector<std::pair<Session::Clip, std::vector<AutoPoint>>> liftAll(Session::Document& s, const std::string& track,
                                                                     const std::unordered_set<std::string>& moving) {
    const Session::Track* tr = playlist(s, track);
    if (!tr) return {};
    const auto plan = liftPlan(spans(tr->clips), moving);
    const auto& lifts = plan.first;
    const auto& staying = plan.second;
    std::unordered_map<std::string, std::vector<AutoPoint>> points;
    for (const auto& l : lifts) {
        points[l.id] = takeAutomation(s, track, l.from, Time::satAdd(l.from, l.shift), l.start);
        followAutomation(s, track, Time::satAdd(l.from, l.shift), l.by, l.lock);
    }
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return {};
    std::unordered_map<std::string, long long> starts;
    for (const auto& c : staying) starts[c.id] = c.start;
    std::vector<Session::Clip> out;
    std::vector<Session::Clip> kept;
    kept.reserve(pl->clips.size());
    for (auto& c : pl->clips) {
        if (points.count(c.id))
            out.push_back(std::move(c));
        else
            kept.push_back(std::move(c));
    }
    pl->clips = std::move(kept);
    for (auto& c : pl->clips) {
        const auto it = starts.find(c.id);
        if (it != starts.end()) c.start = it->second;
    }
    Session::sortPlaylist(*pl);
    std::vector<std::pair<Session::Clip, std::vector<AutoPoint>>> result;
    for (auto& c : out) {
        auto p = points[c.id];
        points.erase(c.id);
        result.emplace_back(std::move(c), std::move(p));
    }
    return result;
}

// after SoundCraft (crates/engine/src/edit.rs split_at)
size_t splitAt(Session::Document& s, const std::string& track, long long at, const std::function<bool(const Session::Clip&)>& autoName,
               bool skipLocked) {
    const TimeCtx t = TimeCtx::of(s);
    size_t nSplit = 0;
    if (const Session::Track* tr = playlist(s, track)) {
        for (const auto& c : tr->clips)
            if (at > c.start && at < ClipOps::end(c) && !(skipLocked && ClipOps::locked(c))) ++nSplit;
    }
    std::vector<std::string> ids;
    ids.reserve(nSplit);
    for (size_t i = 0; i < nSplit; ++i) ids.push_back(Session::newClipId(s));
    auto schemes = std::move(s.autoNames);
    Session::Track* pl = playlistMut(s, track);
    if (!pl) {
        s.autoNames = std::move(schemes);
        return 0;
    }
    std::vector<Session::Clip> out;
    out.reserve(pl->clips.size() + ids.size());
    size_t count = 0;
    ptrdiff_t idIx = (ptrdiff_t)ids.size() - 1;
    for (auto c : pl->clips) {
        if (at > c.start && at < ClipOps::end(c) && !(skipLocked && ClipOps::locked(c)) && idIx >= 0) {
            const std::string nid = ids[(size_t)idIx--];
            if (auto sp = splitClip(c, at, nid, t)) {
                Session::Clip l = sp->first;
                Session::Clip r = sp->second;
                if (autoName(r)) takeAutoName(&schemes, &r);
                out.push_back(std::move(l));
                out.push_back(std::move(r));
                ++count;
                continue;
            }
        }
        out.push_back(std::move(c));
    }
    pl->clips = std::move(out);
    Session::sortPlaylist(*pl);
    s.autoNames = std::move(schemes);
    return count;
}

} // namespace

TimeCtx TimeCtx::of(const Session::Document& s) { return {&s.time, s.sampleRate}; }

long long TimeCtx::ticks(long long samples) const {
    if (!tempo || sampleRate <= 0.0) return samples;
    const double spb = tempo->samplesPerBeat(sampleRate);
    return spb > 0.0 ? (long long)(samples / spb) : samples;
}

// after SoundCraft (crates/engine/src/edit.rs split_clip)
std::optional<std::pair<Session::Clip, Session::Clip>> splitClip(const Session::Clip& clip, long long at, const std::string& newId,
                                                                 const TimeCtx& t) {
    (void)t;
    if (at <= clip.start || at >= ClipOps::end(clip)) return std::nullopt;
    Session::Clip left = clip;
    Session::Clip right = clip;
    right.id = newId;
    ClipOps::trimEndTo(left, at);
    ClipOps::trimStartTo(right, at);
    left.fadeOut = {};
    right.fadeIn = {};
    return std::pair{std::move(left), std::move(right)};
}

size_t separateAt(Session::Document& s, const std::string& track, long long at) {
    return separateAtIf(s, track, at, [](const Session::Clip&) { return true; });
}

size_t separateAtIf(Session::Document& s, const std::string& track, long long at,
                    const std::function<bool(const Session::Clip&)>& autoName) {
    return splitAt(s, track, at, autoName, true);
}

size_t separateAllAt(Session::Document& s, const std::string& track, long long at) {
    return splitAt(s, track, at, [](const Session::Clip&) { return true; }, false);
}

size_t separateAtPoints(Session::Document& s, const std::string& track, std::vector<long long> points) {
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    const TimeCtx t = TimeCtx::of(s);
    auto within = [&](const Session::Clip& c) {
        const auto lo = std::partition_point(points.begin(), points.end(), [&](long long p) { return p <= c.start; });
        const auto hi = std::partition_point(points.begin(), points.end(), [&](long long p) { return p < ClipOps::end(c); });
        return std::pair<size_t, size_t>{(size_t)(lo - points.begin()), (size_t)(hi - points.begin())};
    };
    size_t nSplit = 0;
    if (const Session::Track* tr = playlist(s, track)) {
        for (const auto& c : tr->clips) {
            const auto w = within(c);
            nSplit += w.second > w.first ? w.second - w.first : 0;
        }
    }
    std::vector<std::string> ids;
    ids.reserve(nSplit);
    for (size_t i = 0; i < nSplit; ++i) ids.push_back(Session::newClipId(s));
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return 0;
    std::vector<Session::Clip> out;
    out.reserve(pl->clips.size() + nSplit);
    ptrdiff_t idIx = (ptrdiff_t)ids.size() - 1;
    for (auto c : pl->clips) {
        const auto w = within(c);
        for (size_t pi = w.first; pi < w.second; ++pi) {
            if (idIx < 0) break;
            const std::string nid = ids[(size_t)idIx--];
            if (auto sp = splitClip(c, points[pi], nid, t)) {
                out.push_back(std::move(sp->first));
                c = std::move(sp->second);
            } else
                break;
        }
        out.push_back(std::move(c));
    }
    pl->clips = std::move(out);
    Session::sortPlaylist(*pl);
    return nSplit;
}

void autoNameNewClips(Session::Document& s, const std::string& track, const std::unordered_set<std::string>& old,
                      const std::function<bool(const Session::Clip&)>& autoName) {
    auto schemes = std::move(s.autoNames);
    Session::Track* pl = playlistMut(s, track);
    if (pl) {
        std::vector<Session::Clip*> fresh;
        for (auto& c : pl->clips)
            if (!old.count(c.id) && autoName(c)) fresh.push_back(&c);
        std::sort(fresh.begin(), fresh.end(), [](const Session::Clip* a, const Session::Clip* b) {
            if (a->start != b->start) return a->start < b->start;
            return a->id < b->id;
        });
        for (auto* c : fresh) takeAutoName(&schemes, c);
    }
    s.autoNames = std::move(schemes);
}

std::vector<Session::Clip> clearRange(Session::Document& s, const std::string& track, Time::Range range, bool shuffle) {
    if (range.isEmpty()) return {};
    separateAt(s, track, range.start);
    separateAt(s, track, range.end);
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return {};
    std::vector<Session::Clip> removed;
    std::vector<Session::Clip> kept;
    for (auto& c : pl->clips) {
        if (c.start >= range.start && ClipOps::end(c) <= range.end) {
            Session::Clip r = c;
            r.start = Time::satAdd(r.start, -range.start);
            removed.push_back(std::move(r));
        } else
            kept.push_back(std::move(c));
    }
    pl->clips = std::move(kept);
    if (shuffle) {
        const auto moved = ripple(s, track, range.end, -range.len());
        followAutomation(s, track, range.end, moved.first, moved.second);
    }
    return removed;
}

// after SoundCraft (crates/engine/src/edit.rs put_clips)
void putClips(Session::Document& s, std::vector<std::pair<std::string, Session::Clip>> placed) {
    for (const auto& tc : placed) clearRange(s, tc.first, ClipOps::range(tc.second), false);
    for (auto& tc : placed) {
        Session::Track* pl = playlistMut(s, tc.first);
        if (!pl) continue;
        pl->clips.push_back(std::move(tc.second));
        Session::sortPlaylist(*pl);
    }
}

size_t removeClips(Session::Document& s, const std::vector<std::string>& ids, bool shuffle) {
    std::unordered_set<std::string> set(ids.begin(), ids.end());
    size_t n = 0;
    std::vector<std::string> trackIds;
    for (const auto& t : s.tracks) trackIds.push_back(t.id);
    for (const auto& track : trackIds) {
        if (shuffle) {
            const auto lifted = liftAll(s, track, set);
            n += lifted.size();
        }
        Session::Track* pl = playlistMut(s, track);
        if (!pl) continue;
        const size_t before = pl->clips.size();
        pl->clips.erase(std::remove_if(pl->clips.begin(), pl->clips.end(), [&](const Session::Clip& c) { return set.count(c.id); }),
                         pl->clips.end());
        n += before - pl->clips.size();
    }
    return n;
}

std::vector<Session::Clip> copyRange(const Session::Document& s, const std::string& track, Time::Range range) {
    const TimeCtx t = TimeCtx::of(s);
    const Session::Track* tr = playlist(s, track);
    if (!tr) return {};
    std::vector<Session::Clip> out;
    for (const auto& c : tr->clips) {
        Time::Range isect;
        if (!ClipOps::range(c).intersect(range, &isect)) continue;
        Session::Clip part = c;
        if (isect.start > part.start) {
            const long long old = part.start;
            ClipOps::trimStartTo(part, isect.start);
            rebaseMidiStart(part, old, t);
            part.fadeIn = {};
        }
        if (isect.end < ClipOps::end(part)) {
            ClipOps::trimEndTo(part, isect.end);
            part.fadeOut = {};
        }
        part.start = Time::satAdd(part.start, -range.start);
        out.push_back(std::move(part));
    }
    return out;
}

bool consolidateMidi(Session::Document& s, const std::string& track, Time::Range range) {
    const Session::Track* tr = playlist(s, track);
    if (!tr || !Session::isMidiKind(tr->kind)) return false;
    const auto parts = copyRange(s, track, range);
    Session::MidiSequence merged;
    const long long base = range.start;
    for (const auto& part : parts) {
        if (part.muted || !part.midi) continue;
        const long long partStart = base + part.start;
        const long long at = partStart - base;
        const long long len = part.length;
        for (auto n : part.midi->notes) {
            if (n.start < 0 || n.start >= len) continue;
            n.length = std::min(n.length, len - n.start);
            n.start += at;
            merged.notes.push_back(n);
        }
        for (auto c : part.midi->ctrls) {
            if (c.tick < 0 || c.tick >= len) continue;
            c.tick += at;
            merged.ctrls.push_back(c);
        }
    }
    Session::Clip nc;
    nc.numId = Session::newClipNumId(s);
    nc.id = "clip_" + std::to_string(nc.numId);
    nc.name = tr->name;
    nc.start = range.start;
    nc.length = std::max(1LL, range.len());
    nc.isMidi = true;
    nc.midi = std::move(merged);
    clearRange(s, track, range, false);
    placeClip(s, track, std::move(nc));
    return true;
}

PasteOutcome paste(Session::Document& s, const std::string& track, long long at, const std::vector<Session::Clip>& clips, long long length,
                   bool shuffle) {
    long long len = length;
    for (const auto& c : clips) len = std::max(len, ClipOps::end(c));
    if (shuffle) {
        if (const char* err = makeRoom(s, track, at, len)) return {{}, err};
    }
    PasteOutcome out;
    std::vector<std::pair<std::string, Session::Clip>> placed;
    for (auto c : clips) {
        c.id = Session::newClipId(s);
        c.start = Time::satAdd(c.start, at);
        out.ids.push_back(c.id);
        placed.emplace_back(track, c);
    }
    putClips(s, std::move(placed));
    return out;
}

void placeClip(Session::Document& s, const std::string& track, Session::Clip clip) {
    clearRange(s, track, ClipOps::range(clip), false);
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return;
    pl->clips.push_back(std::move(clip));
    Session::sortPlaylist(*pl);
}

void insertSilence(Session::Document& s, const std::string& track, Time::Range range) {
    if (range.isEmpty()) return;
    separateAt(s, track, range.start);
    shiftFrom(s, track, range.start, range.len());
}

void shiftFrom(Session::Document& s, const std::string& track, long long from, long long by) {
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return;
    for (auto& c : pl->clips)
        if (c.start >= from) c.start = Time::satAdd(c.start, by);
}

bool locked(const Session::Clip& c) { return ClipOps::locked(c); }

long long shuffleRoom(const Session::Document& s, const std::string& track, long long at) {
    const Session::Track* tr = playlist(s, track);
    if (!tr) return kSamplesMax;
    long long lock = kSamplesMax;
    for (const auto& c : tr->clips) {
        if (ClipOps::locked(c) && ClipOps::end(c) > at) lock = std::min(lock, c.start);
    }
    long long end = at;
    for (const auto& c : tr->clips) {
        if (ClipOps::end(c) > at && c.start < lock) end = std::max(end, ClipOps::end(c));
    }
    return Time::satAdd(lock, -end);
}

const char* makeRoom(Session::Document& s, const std::string& track, long long at, long long len) {
    if (len <= 0) return nullptr;
    const Session::Track* tr = playlist(s, track);
    if (!tr) return nullptr;
    size_t covering = 0;
    for (const auto& c : tr->clips)
        if (c.start <= at && at < ClipOps::end(c)) ++covering;
    if (covering > 1) return IN_CROSSFADE;
    if (shuffleRoom(s, track, at) < len) return NO_ROOM;
    separateAt(s, track, at);
    const auto moved = ripple(s, track, at, len);
    followAutomation(s, track, at, moved.first, moved.second);
    return nullptr;
}

size_t trimTo(Session::Document& s, const std::string& track, Time::Range range) {
    const TimeCtx tc = TimeCtx::of(s);
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return 0;
    size_t n = 0;
    for (auto& c : pl->clips) {
        if (ClipOps::locked(c)) continue;
        Time::Range isect;
        const Time::Range cr = ClipOps::range(c);
        if (cr.intersect(range, &isect) && !(isect.start == cr.start && isect.end == cr.end)) {
            const long long old = c.start;
            ClipOps::trimStartTo(c, isect.start);
            rebaseMidiStart(c, old, tc);
            ClipOps::trimEndTo(c, isect.end);
            ++n;
        }
    }
    Session::sortPlaylist(*pl);
    return n;
}

size_t trimStartTo(Session::Document& s, const std::string& track, long long at) {
    const TimeCtx tc = TimeCtx::of(s);
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return 0;
    size_t n = 0;
    for (auto& c : pl->clips) {
        if (ClipOps::locked(c)) continue;
        if (ClipOps::range(c).contains(at) && at > c.start) {
            const long long old = c.start;
            ClipOps::trimStartTo(c, at);
            rebaseMidiStart(c, old, tc);
            ++n;
        }
    }
    return n;
}

size_t trimEndTo(Session::Document& s, const std::string& track, long long at) {
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return 0;
    size_t n = 0;
    for (auto& c : pl->clips) {
        if (ClipOps::locked(c)) continue;
        if (at > c.start && at < ClipOps::end(c)) {
            ClipOps::trimEndTo(c, at);
            ++n;
        }
    }
    return n;
}

size_t heal(Session::Document& s, const std::string& track, Time::Range range) {
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return 0;
    Session::sortPlaylist(*pl);
    std::vector<Session::Clip> out;
    out.reserve(pl->clips.size());
    size_t healed = 0;
    for (auto c : pl->clips) {
        if (!out.empty()) {
            Session::Clip& prev = out.back();
            const bool contiguous = c.start >= ClipOps::end(prev) && ClipOps::end(prev) >= range.start && c.start <= range.end;
            const long long gap = contiguous ? Time::satAdd(c.start, -ClipOps::end(prev)) : 0;
            const bool same =
                !prev.isMidi && !c.isMidi && prev.sourceId == c.sourceId &&
                Time::satAdd(prev.offset, sourceLen(Time::satAdd(prev.length, gap), prev.stretch)) == c.offset;
            const bool samePlayback = prev.muted == c.muted && std::abs(prev.stretch - c.stretch) < 1e-9 && std::abs(prev.gainDb - c.gainDb) < 1e-6;
            if (contiguous && same && samePlayback) {
                if (!prev.gainEnv.empty() || !c.gainEnv.empty()) joinGainEnv(&prev, c);
                prev.length = Time::satAdd(ClipOps::end(c), -prev.start);
                prev.fadeOut = c.fadeOut;
                ++healed;
                continue;
            }
        }
        out.push_back(std::move(c));
    }
    pl->clips = std::move(out);
    return healed;
}

size_t moveClips(Session::Document& s, const std::vector<std::string>& ids, long long delta,
                 const std::vector<std::pair<std::string, std::string>>& dest) {
    std::unordered_set<std::string> set(ids.begin(), ids.end());
    std::optional<long long> limit;
    for (const auto& t : s.tracks) {
        for (const auto& c : t.clips) {
            if (set.count(c.id) && !ClipOps::locked(c)) {
                const long long neg = Time::satAdd(0, -c.start);
                limit = limit ? std::max(*limit, neg) : neg;
            }
        }
    }
    delta = limit ? std::max(delta, *limit) : delta;
    bool allSame = true;
    for (const auto& pr : dest) {
        if (pr.first != pr.second) {
            allSame = false;
            break;
        }
    }
    if (delta == 0 && allSame) return 0;
    std::vector<std::pair<std::string, Session::Clip>> moving;
    for (auto& t : s.tracks) {
        std::vector<Session::Clip> kept;
        for (auto& c : t.clips) {
            if (set.count(c.id) && !ClipOps::locked(c)) {
                moving.emplace_back(t.id, c);
            } else
                kept.push_back(std::move(c));
        }
        t.clips = std::move(kept);
    }
    const size_t n = moving.size();
    std::vector<std::pair<std::string, Session::Clip>> placed;
    placed.reserve(moving.size());
    for (auto& mc : moving) {
        mc.second.start = Time::satAdd(mc.second.start, delta);
        placed.emplace_back(lands(dest, mc.first), std::move(mc.second));
    }
    putClips(s, std::move(placed));
    return n;
}

bool shown(const Session::Document& s, const Session::Track& t) { return Session::trackShown(s, t); }

std::optional<std::vector<std::pair<std::string, std::string>>> destinations(const Session::Document& s, const std::vector<std::string>& ids,
                                                                             const std::string& from, const std::string& to) {
    std::unordered_set<std::string> set(ids.begin(), ids.end());
    auto holds = [&](const Session::Track& t) {
        for (const auto& c : t.clips)
            if (set.count(c.id)) return true;
        return false;
    };
    bool visible = true;
    for (const auto& t : s.tracks) {
        if (t.id == from || t.id == to || holds(t)) {
            if (!shown(s, t)) {
                visible = false;
                break;
            }
        }
    }
    std::vector<const Session::Track*> rows;
    for (const auto& t : s.tracks) {
        if (!visible || shown(s, t)) rows.push_back(&t);
    }
    auto row = [&](const std::string& id) -> std::optional<ptrdiff_t> {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i]->id == id) return (ptrdiff_t)i;
        return std::nullopt;
    };
    const auto fr = row(from);
    const auto tr = row(to);
    if (!fr || !tr) return std::nullopt;
    const ptrdiff_t shift = *tr - *fr;
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& t : s.tracks) {
        if (!holds(t)) continue;
        const auto r = row(t.id);
        if (!r) return std::nullopt;
        const ptrdiff_t di = *r + shift;
        if (di < 0 || di >= (ptrdiff_t)rows.size()) return std::nullopt;
        const Session::Track& d = *rows[(size_t)di];
        if (Session::hasPlaylist(d.kind) && (d.kind == t.kind || (Session::isMidiKind(d.kind) && Session::isMidiKind(t.kind))))
            out.emplace_back(t.id, d.id);
        else
            return std::nullopt;
    }
    return out;
}

std::string lands(const std::vector<std::pair<std::string, std::string>>& dest, const std::string& t) {
    for (const auto& pr : dest)
        if (pr.first == t) return pr.second;
    return t;
}

const Session::Clip* firstMovable(const Session::Document& s, const std::vector<std::string>& ids) {
    const Session::Clip* best = nullptr;
    for (const auto& id : ids) {
        if (auto fc = Session::findClip(s, id)) {
            const Session::Clip& c = *fc->second;
            if (!ClipOps::locked(c) && (!best || c.start < best->start)) best = &c;
        }
    }
    return best;
}

std::vector<long long> shuffleSlots(const Session::Document& s, const std::string& track, const std::vector<std::string>& moving) {
    const Session::Track* tr = playlist(s, track);
    if (!tr) return {};
    const std::unordered_set<std::string> mov(moving.begin(), moving.end());
    const auto plan = liftPlan(spans(tr->clips), mov);
    auto sp = plan.second;
    std::sort(sp.begin(), sp.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
    std::vector<long long> reach;
    long long far = std::numeric_limits<long long>::min();
    for (const auto& c : sp) {
        far = std::max(far, c.end);
        reach.push_back(far);
    }
    auto inside = [&](long long b) {
        const size_t idx =
            (size_t)(std::partition_point(sp.begin(), sp.end(), [&](const Span& c) { return c.start < b; }) - sp.begin());
        if (idx == 0) return false;
        return reach[idx - 1] > b;
    };
    std::vector<long long> slots;
    for (const auto& c : sp) {
        if (!inside(c.start)) slots.push_back(c.start);
        if (!inside(c.end)) slots.push_back(c.end);
    }
    std::sort(slots.begin(), slots.end());
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    return slots;
}

long long nearestSlot(const std::vector<long long>& slots, long long at) {
    const size_t i = (size_t)(std::partition_point(slots.begin(), slots.end(), [&](long long b) { return b < at; }) - slots.begin());
    std::vector<long long> candidates;
    if (i > 0) candidates.push_back(slots[i - 1]);
    if (i < slots.size()) candidates.push_back(slots[i]);
    if (candidates.empty()) return 0;
    long long best = candidates[0];
    long long bd = std::llabs(best - at);
    for (size_t k = 1; k < candidates.size(); ++k) {
        const long long d = std::llabs(candidates[k] - at);
        if (d < bd) {
            bd = d;
            best = candidates[k];
        }
    }
    return best;
}

long long shuffleSlot(const Session::Document& s, const std::string& track, long long at, const std::vector<std::string>& moving) {
    return nearestSlot(shuffleSlots(s, track, moving), at);
}

std::vector<std::pair<std::string, long long>> shuffleGroups(const Session::Document& s, const std::vector<std::string>& ids,
                                                               const std::vector<std::pair<std::string, std::string>>& dest) {
    std::unordered_set<std::string> set(ids.begin(), ids.end());
    std::vector<std::pair<std::string, long long>> groups;
    for (const auto& t : s.tracks) {
        for (const auto& c : t.clips) {
            if (!set.count(c.id) || ClipOps::locked(c)) continue;
            const std::string d = lands(dest, t.id);
            auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == d; });
            if (it != groups.end())
                it->second = std::min(it->second, c.start);
            else
                groups.emplace_back(d, c.start);
        }
    }
    return groups;
}

ShuffleOutcome shuffleClips(Session::Document& s, const std::vector<std::string>& ids, long long delta,
                            const std::vector<std::pair<std::string, std::string>>& dest) {
    const auto groups = shuffleGroups(s, ids, dest);
    std::vector<std::tuple<std::string, long long, long long>> planned;
    for (const auto& g : groups)
        planned.emplace_back(g.first, g.second, shuffleSlot(s, g.first, Time::satAdd(g.second, delta), ids));
    std::unordered_set<std::string> moving(ids.begin(), ids.end());
    std::vector<std::string> sources;
    for (const auto& t : s.tracks) {
        for (const auto& c : t.clips) {
            if (moving.count(c.id) && !ClipOps::locked(c)) {
                sources.push_back(t.id);
                break;
            }
        }
    }
    std::vector<std::tuple<std::string, Session::Clip, std::vector<AutoPoint>>> lifted;
    for (const auto& src : sources) {
        for (auto& item : liftAll(s, src, moving)) {
            lifted.emplace_back(lands(dest, src), std::move(item.first), std::move(item.second));
        }
    }
    for (const auto& plan : planned) {
        const std::string d = std::get<0>(plan);
        const long long first = std::get<1>(plan);
        const long long at = std::get<2>(plan);
        std::vector<const std::tuple<std::string, Session::Clip, std::vector<AutoPoint>>*> block;
        for (const auto& l : lifted) {
            if (std::get<0>(l) == d) block.push_back(&l);
        }
        long long len = first;
        for (const auto* l : block) len = std::max(len, ClipOps::end(std::get<1>(*l)));
        len = Time::satAdd(len, -first);
        if (const char* err = makeRoom(s, d, at, len)) return {0, err};
        std::vector<Session::Clip> placed;
        for (const auto* l : block) {
            Session::Clip c = std::get<1>(*l);
            c.start = Time::satAdd(at, Time::satAdd(c.start, -first));
            putAutomation(s, d, c.start, std::get<2>(*l));
            placed.push_back(std::move(c));
        }
        Session::Track* pl = playlistMut(s, d);
        if (pl) {
            pl->clips.insert(pl->clips.end(), placed.begin(), placed.end());
            Session::sortPlaylist(*pl);
        }
    }
    return {lifted.size(), nullptr};
}

long long gridDelta(const Session::Document& s, const Session::Clip& clip, long long raw, bool relative, const std::vector<std::string>& moving) {
    const long long sync = (clip.syncPoint >= 0 && clip.syncPoint < clip.length) ? clip.syncPoint : 0;
    const long long anchor = Time::satAdd(clip.start, sync);
    const long long from =
        relative ? TimeGrid::gridLineAtOrBeforeExcluding(s, anchor, moving) : anchor;
    return Time::satAdd(TimeGrid::snapToGridExcluding(s, Time::satAdd(from, raw), moving), -from);
}

std::optional<Session::Clip> trimClip(Session::Document& s, const std::string& id, std::optional<long long> start, std::optional<long long> end,
                                      bool shuffle) {
    auto fc = Session::findClip(s, id);
    if (!fc || fc->second->editLocked) return std::nullopt;
    const std::string track = fc->first;
    const Session::Clip old = *fc->second;
    long long lo = std::numeric_limits<long long>::min();
    long long hi = kSamplesMax;
    if (shuffle) {
        const long long room = std::max(shuffleRoom(s, track, ClipOps::end(old)), 0LL);
        lo = Time::satAdd(old.start, -room);
        hi = Time::satAdd(ClipOps::end(old), room);
    } else {
        std::vector<const Session::Clip*> others;
        if (const Session::Track* tr = playlist(s, track)) {
            for (const auto& c : tr->clips)
                if (c.id != id) others.push_back(&c);
        }
        for (const auto* p : others) {
            if (p->start < old.start) lo = std::max(lo, std::min(ClipOps::end(*p), old.start));
        }
        for (const auto* n : others) {
            if (n->start > old.start) hi = std::min(hi, std::max(n->start, ClipOps::end(old)));
        }
        lo = std::max(lo, 0LL);
    }
    auto fileBounds = [&](const Session::Clip& c) -> std::pair<long long, long long> {
        const double stretch = std::isfinite(c.stretch) && c.stretch > 0.0 ? c.stretch : 1.0;
        auto scale = [&](long long n) {
            return std::abs(stretch - 1.0) >= 1e-9 ? (long long)std::llround(double(n) * stretch) : n;
        };
        const long long so = scale(ClipOps::sourceOffset(c));
        const long long f0 = Time::satAdd(c.start, -so);
        const auto ml = materialLen(s, c);
        const long long f1 = ml ? Time::satAdd(f0, scale(*ml)) : ClipOps::end(c);
        return {std::min(f0, c.start), std::max(f1, ClipOps::end(c))};
    };
    const TimeCtx t = TimeCtx::of(s);
    Session::Clip c = old;
    if (start) {
        const auto fb = fileBounds(c);
        const long long at = std::max(*start, std::max(lo, fb.first));
        ClipOps::trimStartTo(c, at);
        rebaseMidiStart(c, old.start, t);
        if (shuffle) c.start = old.start;
    }
    if (end) {
        const auto fb = fileBounds(c);
        const long long at = std::min(*end, std::min(hi, fb.second));
        ClipOps::trimEndTo(c, at);
    }
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return std::nullopt;
    Session::Clip* pc = Session::clipMut(s, track, id);
    if (!pc) return std::nullopt;
    *pc = c;
    Session::sortPlaylist(*pl);
    if (shuffle) {
        const auto moved = ripple(s, track, Time::satAdd(old.start, 1), Time::satAdd(ClipOps::end(c), -ClipOps::end(old)));
        followAutomation(s, track, ClipOps::end(old), moved.first, moved.second);
    }
    return c;
}

size_t createFades(Session::Document& s, const std::string& track, Time::Range range, Session::FadeShape shape) {
    if (range.isEmpty()) return 0;
    std::vector<std::pair<std::string, long long>> handles;
    if (const Session::Track* tr = playlist(s, track)) {
        for (const auto& c : tr->clips) {
            const auto ml = materialLen(s, c);
            const long long h = ml ? Time::satAdd(*ml, -ClipOps::sourceOffset(c)) : 0;
            handles.emplace_back(c.id, Time::satAdd(h, -c.length));
        }
    }
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return 0;
    Session::sortPlaylist(*pl);
    size_t made = 0;
    std::unordered_set<std::string> xfaded;
    const size_t n = pl->clips.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        const Session::Clip& a = pl->clips[i];
        const Session::Clip& b = pl->clips[i + 1];
        const long long boundary = ClipOps::end(a);
        if (b.start != boundary || range.start >= boundary || range.end <= boundary) continue;
        const long long before = Time::satAdd(boundary, -std::max(range.start, a.start));
        const long long after = std::min(range.end, ClipOps::end(b)) - boundary;
        long long aHandle = 0;
        for (const auto& h : handles)
            if (h.first == a.id) aHandle = h.second;
        const long long bHandle = ClipOps::sourceOffset(b);
        const long long extA = std::max(0LL, std::min(after, aHandle));
        const long long extB = std::max(0LL, std::min(before, bHandle));
        Session::Fade fadeOut = a.fadeOut;
        fadeOut.len = std::min(Time::satAdd(extA, before), pl->clips[i].length);
        fadeOut.shape = shape;
        fadeOut.curvature = 0.0f;
        pl->clips[i].length = Time::satAdd(pl->clips[i].length, extA);
        pl->clips[i].fadeOut = fadeOut;
        const long long old = pl->clips[i + 1].start;
        ClipOps::trimStartTo(pl->clips[i + 1], Time::satAdd(old, -extB));
        Session::Fade fadeIn = pl->clips[i + 1].fadeIn;
        fadeIn.len = std::min(Time::satAdd(extB, after), pl->clips[i + 1].length);
        fadeIn.shape = shape;
        fadeIn.curvature = 0.0f;
        pl->clips[i + 1].fadeIn = fadeIn;
        ++made;
        xfaded.insert(a.id);
        xfaded.insert(b.id);
    }
    for (auto& c : pl->clips) {
        if (xfaded.count(c.id)) continue;
        if (range.start <= c.start && range.end > c.start && range.end < ClipOps::end(c)) {
            c.fadeIn = {range.end - c.start, shape, 0.0f};
            ++made;
        } else if (range.end >= ClipOps::end(c) && range.start > c.start && range.start < ClipOps::end(c)) {
            c.fadeOut = {ClipOps::end(c) - range.start, shape, 0.0f};
            ++made;
        }
    }
    return made;
}

size_t deleteFades(Session::Document& s, const std::string& track, Time::Range range) {
    Session::Track* pl = playlistMut(s, track);
    if (!pl) return 0;
    size_t n = 0;
    std::unordered_set<std::string> touched;
    for (auto& c : pl->clips) {
        if (ClipOps::range(c).overlaps(range) || (range.isEmpty() && ClipOps::range(c).contains(range.start))) {
            touched.insert(c.id);
            if (c.fadeIn.len > 0 || c.fadeOut.len > 0) ++n;
            c.fadeIn = {};
            c.fadeOut = {};
        }
    }
    Session::sortPlaylist(*pl);
    for (size_t i = 0; i + 1 < pl->clips.size(); ++i) {
        const long long aEnd = ClipOps::end(pl->clips[i]);
        const long long bStart = pl->clips[i + 1].start;
        const bool inRange = touched.count(pl->clips[i].id) || touched.count(pl->clips[i + 1].id);
        if (inRange && aEnd > bStart) {
            const long long mid = Time::satAdd(bStart, (aEnd - bStart) / 2);
            ClipOps::trimEndTo(pl->clips[i], mid);
            ClipOps::trimStartTo(pl->clips[i + 1], mid);
        }
    }
    Session::sortPlaylist(*pl);
    return n;
}

} // namespace EngEdit