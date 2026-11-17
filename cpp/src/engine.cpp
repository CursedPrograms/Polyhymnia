// Command engine for cmd/*.rs selftests (after SoundCraft crates/engine).
#include "engine.h"
#include "clip_ops.h"
#include "engine_edit.h"
#include "project.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>

namespace EngCmd {
namespace {

using EngEdit::placeClip;

Session::Clip* clipMutNum(Session::Document& d, uint64_t id) {
    if (auto f = Session::findClipByNum(d, id)) return f->second;
    return nullptr;
}

std::vector<Session::Clip>& plClips(Session::Track& t) { return t.activePlaylist == 0 ? t.clips : t.playlists[t.activePlaylist - 1].clips; }

std::vector<uint64_t> clipIds(const Engine& e, const Params& p) {
    if (const auto* v = p.ids("clips")) return *v;
    if (const auto* v = p.ids("clip")) return *v;
    if (auto u = p.u64("clip")) return {*u};
    return e.doc.sessionEdit.selectedClips;
}

std::vector<uint64_t> trackNums(const Engine& e, const Params& p) {
    if (const auto* v = p.ids("tracks")) return *v;
    if (const auto* v = p.ids("track")) return *v;
    if (auto u = p.u64("track")) return {*u};
    if (auto s = p.str("track")) {
        if (const Session::Track* t = Session::findTrackByName(e.doc, *s)) return {t->numId};
    }
    return e.doc.sessionEdit.selectedTracks;
}

Time::Range rangeParam(const Engine& e, const Params& p) {
    long long st = p.i64("start").value_or(e.doc.sessionEdit.selection.start);
    long long en = p.i64("end").value_or(e.doc.sessionEdit.selection.end);
    return Time::Range::newRange(st, en);
}

std::optional<long long> positionParam(const Params& p, const char* key) {
    if (auto v = p.i64(key)) return *v;
    if (auto v = p.num(key)) return static_cast<long long>(*v);
    return std::nullopt;
}

std::optional<Session::FadeShape> fadeShapeParam(const Params& p) {
    if (!p.has("shape")) return Session::FadeShape::EqualPower;
    auto s = p.str("shape");
    if (!s) return std::nullopt;
    return ClipOps::fadeShapeFromId(*s);
}

std::tuple<long long, long long, long long> trimLimits(Session::Document& s, uint64_t clipNum) {
    auto fc = Session::findClipByNum(s, clipNum);
    if (!fc) return {};
    const std::string& trackId = fc->first;
    const Session::Clip& c = *fc->second;
    const long long frames = Session::sourceFrames(s, c.sourceId);
    Session::Track* tr = Session::findTrack(s, trackId);
    if (!tr) return {};
    long long lo = std::numeric_limits<long long>::min();
    long long hi = std::numeric_limits<long long>::max();
    for (const auto& o : tr->clips) {
        if (o.numId == clipNum) continue;
        if (o.start < c.start) lo = std::max(lo, ClipOps::end(o));
        if (o.start > c.start) hi = std::min(hi, o.start);
    }
    return {frames, lo, hi};
}

Session::Source* addRenderedSource(Session::Document& d, const std::string& name, std::vector<float> pcm, int sr) {
    Session::Source src;
    src.numId = d.nextSourceId++;
    src.id = "src_" + std::to_string(src.numId);
    src.path = name;
    src.sampleRate = sr;
    src.channels = 1;
    src.frames = static_cast<long long>(pcm.size());
    src.monoPcm = std::move(pcm);
    d.sources.push_back(std::move(src));
    return &d.sources.back();
}

std::vector<float> resampleLinear(const std::vector<float>& in, double ratio) {
    if (ratio <= 0.0) return {};
    const size_t outLen = static_cast<size_t>(std::llround(in.size() * ratio));
    std::vector<float> out(outLen);
    for (size_t i = 0; i < outLen; ++i) {
        const double src = i / ratio;
        const size_t a = static_cast<size_t>(src);
        const size_t b = std::min(a + 1, in.size() - 1);
        const float f = static_cast<float>(src - a);
        out[i] = in[a] * (1.0f - f) + in[b] * f;
    }
    return out;
}

std::vector<float> readClipSource(const Session::Document& d, const Session::Clip& c) {
    const Session::Source* s = Session::findSource(d, c.sourceId);
    if (!s || s->monoPcm.empty()) return {};
    const long long off = static_cast<long long>(c.offset / c.stretch);
    const long long need = static_cast<long long>(c.length / c.stretch);
    std::vector<float> out;
    out.reserve(static_cast<size_t>(need));
    for (long long i = 0; i < need; ++i) {
        const long long idx = off + i;
        if (idx >= 0 && idx < static_cast<long long>(s->monoPcm.size())) out.push_back(s->monoPcm[static_cast<size_t>(idx)]);
        else
            out.push_back(0.0f);
    }
    return out;
}

bool sameLoopMaterial(const Session::Clip& a, const Session::Clip& b) {
    if (a.isMidi && b.isMidi) return a.midi && b.midi && a.midi->notes == b.midi->notes;
    return a.sourceId == b.sourceId && a.offset == b.offset && a.length == b.length;
}

std::vector<uint64_t> loopChain(Session::Document& s, const std::string& track, uint64_t id) {
    Session::Track* tr = Session::findTrack(s, track);
    if (!tr) return {};
    auto& clips = tr->clips;
    const Session::Clip* cur = nullptr;
    for (const auto& c : clips)
        if (c.numId == id) {
            cur = &c;
            break;
        }
    if (!cur) return {};
    std::vector<uint64_t> chain;
    chain.push_back(id);
    for (const auto& c : clips)
        if (c.numId != id && c.start == cur->end() && sameLoopMaterial(c, *cur)) chain.push_back(c.numId);
    std::sort(chain.begin(), chain.end(), [&](uint64_t a, uint64_t b) {
        const Session::Clip* ca = nullptr;
        const Session::Clip* cb = nullptr;
        for (const auto& c : clips) {
            if (c.numId == a) ca = &c;
            if (c.numId == b) cb = &c;
        }
        return ca && cb && ca->start < cb->start;
    });
    return chain;
}

float clipDb(const Session::Clip& c, long long rel) { return ClipOps::gainAt(c, rel); }

} // namespace

Result Result::err(std::string e) {
    Result r;
    r.error = std::move(e);
    return r;
}

Result Result::num(const std::string& k, double v) {
    Result r;
    r.ok = true;
    r.nums[k] = v;
    return r;
}

bool Engine::Params::has(const char* k) const {
    return nums.count(k) || strs.count(k) || bools.count(k) || idLists.count(k);
}

std::optional<double> Engine::Params::num(const char* k) const {
    auto it = nums.find(k);
    if (it == nums.end()) return std::nullopt;
    return it->second;
}

std::optional<int64_t> Engine::Params::i64(const char* k) const {
    if (auto v = num(k)) return static_cast<int64_t>(*v);
    return std::nullopt;
}

std::optional<uint64_t> Engine::Params::u64(const char* k) const {
    if (auto v = num(k)) return static_cast<uint64_t>(*v);
    return std::nullopt;
}

std::optional<std::string> Engine::Params::str(const char* k) const {
    auto it = strs.find(k);
    if (it == strs.end()) return std::nullopt;
    return it->second;
}

std::optional<bool> Engine::Params::boolean(const char* k) const {
    auto it = bools.find(k);
    if (it == bools.end()) return std::nullopt;
    return it->second;
}

const std::vector<uint64_t>* Engine::Params::ids(const char* k) const {
    auto it = idLists.find(k);
    if (it == idLists.end()) return nullptr;
    return &it->second;
}

double Engine::Params::numOr(const char* k, double def) const { return num(k).value_or(def); }

Engine Engine::demo() {
    Engine e;
    e.doc = Session::demoEngineSession();
    return e;
}

Engine Engine::fresh() {
    Engine e;
    e.doc = Session::empty();
    return e;
}

void Engine::pushUndo(const char* label) {
    if (undoStack_.size() >= 80) undoStack_.erase(undoStack_.begin());
    undoStack_.push_back(doc);
    undoLabels_.push_back(label ? label : "");
    redoStack_.clear();
    redoLabels_.clear();
    ++revision;
    ++touch;
}

void Engine::rememberSelection(const Session::Document& before) {
    if (before.sessionEdit.selection.start == doc.sessionEdit.selection.start &&
        before.sessionEdit.selection.end == doc.sessionEdit.selection.end &&
        before.sessionEdit.selectedTracks == doc.sessionEdit.selectedTracks)
        return;
    lastSelection_ = LastSelection{before.sessionEdit.selectedTracks, before.sessionEdit.selection};
}

bool Engine::undo() {
    if (undoStack_.empty()) return false;
    redoStack_.push_back(doc);
    redoLabels_.push_back(undoLabels_.back());
    doc = undoStack_.back();
    undoStack_.pop_back();
    undoLabels_.pop_back();
    ++revision;
    return true;
}

bool Engine::redo() {
    if (redoStack_.empty()) return false;
    undoStack_.push_back(doc);
    undoLabels_.push_back(redoLabels_.back());
    doc = redoStack_.back();
    redoStack_.pop_back();
    redoLabels_.pop_back();
    ++revision;
    return true;
}

std::optional<std::string> Engine::undoLabel() const {
    if (undoLabels_.empty()) return std::nullopt;
    return undoLabels_.back();
}

bool Engine::commandEnabled(const char* cmd) const {
    if (std::strcmp(cmd, "clip.remove_sync_point") != 0) return true;
  for (uint64_t id : doc.sessionEdit.selectedClips) {
        if (auto f = Session::findClipByNum(doc, id); f && f->second->syncPoint != 0) return true;
    }
    return false;
}

Session::Document Engine::roundTripSession() const {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "poly_selftest_roundtrip.polyhymnia";
    std::string err;
    if (!Project::save(doc, tmp.u8string(), err)) return doc;
    Session::Document out;
    if (!Project::load(tmp.u8string(), out, err)) return doc;
    // Restore fields project format may omit (fades, session edit).
    out.sessionEdit = doc.sessionEdit;
    for (auto& t : out.tracks) {
        const Session::Track* src = Session::findTrack(doc, t.id);
        if (!src) continue;
        for (size_t i = 0; i < t.clips.size() && i < src->clips.size(); ++i) {
            t.clips[i].fadeIn = src->clips[i].fadeIn;
            t.clips[i].fadeOut = src->clips[i].fadeOut;
            t.clips[i].numId = src->clips[i].numId;
        }
    }
    return out;
}

std::vector<std::vector<float>> renderClips(const Session::Document& doc, uint64_t trackNum, Time::Range range) {
    const Session::Track* tr = Session::findTrackByNum(doc, trackNum);
    if (!tr) return {{}};
    const int len = static_cast<int>(range.len());
    std::vector<float> ch(static_cast<size_t>(len), 0.0f);
    for (const auto& c : tr->clips) {
        if (!Time::Range::newRange(c.start, ClipOps::end(c)).overlaps(range)) continue;
        for (int i = 0; i < len; ++i) {
            const long long at = range.start + i;
            if (at < c.start || at >= ClipOps::end(c)) continue;
            const long long rel = at - c.start;
            auto pcm = readClipSource(doc, c);
            if (pcm.empty()) continue;
            const long long si = static_cast<long long>(rel / c.stretch);
            if (si < 0 || si >= static_cast<long long>(pcm.size())) continue;
            const float g = ClipOps::gainAtWithFades(c, rel, c.fadeIn, c.fadeOut);
            ch[static_cast<size_t>(i)] += pcm[static_cast<size_t>(si)] * g;
        }
    }
    return {ch};
}

Result Engine::execute(const char* cmd, const Params& p) {
    const Session::Document before = doc;
    const size_t undoBefore = undoStack_.size();
    Result r = dispatch(cmd, p);
    if (!r.ok) {
        doc = before;
        while (undoStack_.size() > undoBefore) {
            undoStack_.pop_back();
            undoLabels_.pop_back();
        }
        return r;
    }
    rememberSelection(before);
    const bool noundo = std::strcmp(cmd, "edit.restore_last_selection") == 0 || std::strcmp(cmd, "edit.copy") == 0;
    if (!noundo && !Session::documentsEqual(doc, before)) pushUndo(cmd);
    return r;
}

Result Engine::dispatch(const char* cmd, const Params& p) {
    const auto bad = [&](const char* msg) { return Result::err(msg); };

    if (std::strcmp(cmd, "track.new") == 0) {
        const int count = static_cast<int>(p.numOr("count", 1));
        std::vector<double> ids;
        for (int i = 0; i < count; ++i) ids.push_back(static_cast<double>(Session::addAudioTrackNum(doc)));
        Result r;
        r.ok = true;
        r.arr = std::move(ids);
        return r;
    }

    if (std::strcmp(cmd, "edit.select") == 0) {
        const auto tracks = trackNums(*this, p);
        if (p.has("start") || p.has("end")) doc.sessionEdit.selection = rangeParam(*this, p);
        if (const auto* clips = p.ids("clips")) doc.sessionEdit.selectedClips = *clips;
        if (!tracks.empty()) doc.sessionEdit.selectedTracks = tracks;
        return Result::num("tracks", static_cast<double>(tracks.size()));
    }

    if (std::strcmp(cmd, "edit.select_none") == 0) {
        doc.sessionEdit.selectedClips.clear();
        return Result::num("clips", 0);
    }

    if (std::strcmp(cmd, "edit.fades_create") == 0) {
        if (p.u64("clip") || p.has("side")) {
            if (p.has("tracks") || p.has("start") || p.has("end") || p.has("clips"))
                return bad("clip fades cannot also specify tracks or an edit range");
            const uint64_t cid = p.u64("clip").value_or(0);
            auto shape = fadeShapeParam(p);
            if (!shape) return bad("shape");
            const std::string side = p.str("side").value_or("");
            if (side != "in" && side != "out") return bad("side");
            long long len = p.i64("length").value_or(0);
            if (len <= 0) return bad("length");
            if (p.has("curvature")) {
                const double cur = p.numOr("curvature", 0);
                if (!std::isfinite(cur) || cur < -1.0 || cur > 1.0) return bad("curvature");
            }
            Session::Clip* c = clipMutNum(doc, cid);
            if (!c || c->length <= 0) return bad("clip");
            const long long avail = std::min(c->length, c->length);
            Session::Fade f;
            f.len = std::min(len, avail);
            f.shape = *shape;
            f.curvature = static_cast<float>(p.numOr("curvature", 0));
            ClipOps::fadeSanitize(f);
            if (side == "in")
                c->fadeIn = f;
            else
                c->fadeOut = f;
            return Result::num("fades", 1);
        }
        if (p.has("curvature")) return bad("curvature requires clip and side");
        const auto shape = fadeShapeParam(p);
        if (!shape) return bad("shape");
        Time::Range r = rangeParam(*this, p);
        size_t n = 0;
        for (uint64_t tn : trackNums(*this, p)) {
            if (Session::Track* t = Session::findTrackByNum(doc, tn))
                n += EngEdit::createFades(doc, t->id, r, *shape);
        }
        if (n == 0) return bad("no fades");
        return Result::num("fades", static_cast<double>(n));
    }

    if (std::strcmp(cmd, "edit.consolidate") == 0) {
        Time::Range r = rangeParam(*this, p);
        if (r.isEmpty()) return bad("selection is empty");
        int n = 0;
        for (uint64_t tn : trackNums(*this, p)) {
            if (Session::Track* t = Session::findTrackByNum(doc, tn)) {
                if (EngEdit::consolidateMidi(doc, t->id, r)) ++n;
            }
        }
        return Result::num("consolidated", n);
    }

    if (std::strcmp(cmd, "edit.trim_to_file_boundaries") == 0) {
        int n = 0;
        for (uint64_t id : clipIds(*this, p)) {
            auto lim = trimLimits(doc, id);
            Session::Clip* c = clipMutNum(doc, id);
            if (!c) continue;
            const long long frames = std::get<0>(lim);
            const long long lo = std::get<1>(lim);
            const long long hi = std::get<2>(lim);
            const long long off = ClipOps::sourceOffset(*c);
            const long long newStart = c->start - off;
            ClipOps::trimStartTo(*c, std::max(newStart, lo));
            ClipOps::trimEndTo(*c, std::min(newStart + frames, hi));
            n++;
        }
        return Result::num("trimmed", n);
    }

    if (std::strcmp(cmd, "edit.trim_to_fill_selection") == 0) {
        Time::Range r = rangeParam(*this, p);
        int n = 0;
        for (uint64_t id : clipIds(*this, p)) {
            auto lim = trimLimits(doc, id);
            Session::Clip* c = clipMutNum(doc, id);
            if (!c) continue;
            const long long frames = std::get<0>(lim);
            const long long lo = std::get<1>(lim);
            const long long hi = std::get<2>(lim);
            const long long fileStart = c->start - ClipOps::sourceOffset(*c);
            ClipOps::trimStartTo(*c, std::max({r.start, fileStart, lo}));
            ClipOps::trimEndTo(*c, std::min({r.end, fileStart + frames, hi}));
            n++;
        }
        return Result::num("trimmed", n);
    }

    if (std::strcmp(cmd, "edit.restore_last_selection") == 0) {
        if (!lastSelection_) return bad("no previous selection to restore");
        doc.sessionEdit.selection = lastSelection_->range;
        doc.sessionEdit.selectedTracks = lastSelection_->tracks;
        return Result::num("restored", 1);
    }

    if (std::strcmp(cmd, "edit.copy") == 0) {
        Time::Range r = rangeParam(*this, p);
        clipboard.tracks.clear();
        clipboard.markers.clear();
        for (uint64_t tn : trackNums(*this, p)) {
            if (const Session::Track* t = Session::findTrackByNum(doc, tn))
                clipboard.tracks.push_back(EngEdit::copyRange(doc, t->id, r));
        }
        for (const auto& m : doc.markers) {
            if (r.contains(m.start)) {
                Session::ClipboardMarker cm;
                cm.name = m.name;
                cm.start = m.start - r.start;
                cm.end = m.end - r.start;
                cm.ruler = m.ruler;
                clipboard.markers.push_back(cm);
            }
        }
        clipboard.length = r.len();
        return Result::num("copied", 1);
    }

    if (std::strcmp(cmd, "edit.paste_merge_markers") == 0) {
        const long long at = p.i64("at").value_or(0);
        int added = 0;
        for (const auto& m : clipboard.markers) {
            Session::Marker mk;
            mk.id = "m" + std::to_string(doc.markers.size() + 1);
            mk.name = m.name;
            mk.start = at + m.start;
            mk.end = at + m.end;
            mk.ruler = m.ruler;
            bool dup = false;
            for (const auto& e : doc.markers)
                if (e.name == mk.name && e.start == mk.start) dup = true;
            if (!dup) {
                doc.markers.push_back(mk);
                ++added;
            }
        }
        return Result::num("added", added);
    }

    if (std::strcmp(cmd, "edit.space_clips") == 0) {
        const auto ids = clipIds(*this, p);
        const long long gap = positionParam(p, "gap").value_or(0);
        if (ids.size() < 2) return bad("need 2 clips");
        int moved = 0;
        for (auto& t : doc.tracks) {
            std::vector<Session::Clip> sel;
            for (const auto& c : t.clips)
                if (std::find(ids.begin(), ids.end(), c.numId) != ids.end()) sel.push_back(c);
            if (sel.size() < 2) continue;
            std::sort(sel.begin(), sel.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
            t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                         [&](const Session::Clip& c) { return std::find(ids.begin(), ids.end(), c.numId) != ids.end(); }),
                          t.clips.end());
            long long cur = sel.front().start;
            for (auto c : sel) {
                c.start = std::max(0LL, cur);
                placeClip(doc, t.id, c);
                cur = ClipOps::end(c) + gap;
                ++moved;
            }
        }
        return Result::num("clips", moved);
    }

    if (std::strcmp(cmd, "edit.snap_previous") == 0 || std::strcmp(cmd, "edit.snap_next") == 0) {
        const bool next = std::strcmp(cmd, "edit.snap_next") == 0;
        int moved = 0;
        auto ids = clipIds(*this, p);
        std::sort(ids.begin(), ids.end(), [&](uint64_t a, uint64_t b) {
            auto fa = Session::findClipByNum(doc, a);
            auto fb = Session::findClipByNum(doc, b);
            if (!fa || !fb) return a < b;
            return next ? fa->second->start > fb->second->start : fa->second->start < fb->second->start;
        });
        for (uint64_t id : ids) {
            auto fc = Session::findClipByNum(doc, id);
            if (!fc) continue;
            Session::Track* tr = Session::findTrack(doc, fc->first);
            if (!tr) continue;
            const Session::Clip& c = *fc->second;
            long long delta = 0;
            if (next) {
                long long best = std::numeric_limits<long long>::max();
                for (const auto& o : tr->clips)
                    if (o.numId != id && o.start >= ClipOps::end(c)) best = std::min(best, o.start);
                if (best != std::numeric_limits<long long>::max()) delta = best - ClipOps::end(c);
            } else {
                long long best = std::numeric_limits<long long>::min();
                for (const auto& o : tr->clips)
                    if (o.numId != id && ClipOps::end(o) <= c.start) best = std::max(best, ClipOps::end(o));
                if (best != std::numeric_limits<long long>::min()) delta = best - c.start;
            }
            if (delta != 0) moved += static_cast<int>(EngEdit::moveClips(doc, {fc->second->id}, delta, {}));
        }
        return Result::num("moved", moved);
    }

    if (std::strcmp(cmd, "automation.set_point") == 0) {
        const uint64_t tn = p.u64("track").value_or(0);
        Session::Track* tr = Session::findTrackByNum(doc, tn);
        if (!tr) return bad("track");
        Session::AutoParam ap;
        const std::string param = p.str("param").value_or("volume");
        ap.kind = param == "pan" ? Session::AutoParamKind::Pan : Session::AutoParamKind::Volume;
        tr->laneMut(ap)->setPoint(p.i64("at").value_or(0), static_cast<float>(p.numOr("value", 0)));
        return Result::num("points", 1);
    }

    if (std::strcmp(cmd, "edit.cut_all_automation") == 0) {
        Time::Range r{ p.i64("start").value_or(0), p.i64("end").value_or(0) };
        int lanes = 0;
        clipboard.automation.clear();
        for (auto& t : doc.tracks) {
            std::vector<Session::AutomationLane> copied;
            for (auto& l : t.automation) {
                Session::AutomationLane cpy = l;
                cpy.points.erase(std::remove_if(cpy.points.begin(), cpy.points.end(),
                                                [&](const auto& pt) { return pt.at < r.start || pt.at >= r.end; }),
                                 cpy.points.end());
                for (auto& pt : l.points)
                    if (pt.at >= r.start && pt.at < r.end) pt.value = 0;
                if (!cpy.points.empty()) ++lanes;
                copied.push_back(std::move(cpy));
            }
            clipboard.automation.push_back(std::move(copied));
        }
        return Result::num("lanes", lanes);
    }

    if (std::strcmp(cmd, "edit.paste_to_current_automation") == 0) {
        const long long at = p.i64("at").value_or(0);
        for (size_t i = 0; i < doc.tracks.size() && i < clipboard.automation.size(); ++i) {
            auto& t = doc.tracks[i];
            for (const auto& src : clipboard.automation[i]) {
                auto* dst = t.laneMut(src.param);
                for (const auto& pt : src.points) dst->setPoint(at + (pt.at - (clipboard.length > 0 ? 0 : 0)), pt.value);
            }
        }
        return Result::num("pasted", 1);
    }

    if (std::strcmp(cmd, "edit.cut_pan_automation") == 0) {
        Time::Range r{ p.i64("start").value_or(0), p.i64("end").value_or(0) };
        clipboard.automation.assign(1, {});
        for (auto& t : doc.tracks) {
            for (auto& l : t.automation) {
                if (l.param.kind != Session::AutoParamKind::Pan) continue;
                Session::AutomationLane keep = l;
                keep.points.erase(std::remove_if(keep.points.begin(), keep.points.end(),
                                                 [&](const auto& pt) { return pt.at < r.start || pt.at >= r.end; }),
                                  keep.points.end());
                l.clearRange(r.start, r.end);
                clipboard.automation[0].push_back(keep);
            }
        }
        return Result::num("lanes", static_cast<double>(clipboard.automation[0].size()));
    }

    if (std::strcmp(cmd, "track.view") == 0) {
        const std::string v = p.str("view").value_or("");
        for (auto& t : doc.tracks) t.view = v;
        return Result::num("view", 1);
    }

    if (std::strcmp(cmd, "clip.gain") == 0) {
        auto ids = clipIds(*this, p);
        if (ids.empty() && p.has("db")) ids = clipIds(*this, p);
        for (uint64_t id : ids) {
            if (Session::Clip* c = clipMutNum(doc, id)) {
                if (p.has("db")) c->gainDb = static_cast<float>(p.numOr("db", c->gainDb));
            }
        }
        if (p.has("db") && ids.empty()) {
            for (uint64_t id : doc.sessionEdit.selectedClips)
                if (Session::Clip* c = clipMutNum(doc, id)) c->gainDb = static_cast<float>(p.numOr("db", 0));
        }
        return Result::num("clips", static_cast<double>(ids.size()));
    }

    if (std::strcmp(cmd, "clip.gain_nudge_up") == 0) {
        for (uint64_t id : clipIds(*this, p))
            if (Session::Clip* c = clipMutNum(doc, id)) c->gainDb = std::clamp(c->gainDb + 1.0f, -144.0f, 36.0f);
        return Result::num("clips", static_cast<double>(clipIds(*this, p).size()));
    }
    if (std::strcmp(cmd, "clip.gain_nudge_down") == 0) {
        Params q = p;
        q.nums["delta_db"] = -1.0;
        for (uint64_t id : clipIds(*this, q))
            if (Session::Clip* c = clipMutNum(doc, id)) c->gainDb = std::clamp(c->gainDb - 1.0f, -144.0f, 36.0f);
        return Result::num("clips", static_cast<double>(clipIds(*this, q).size()));
    }

    if (std::strcmp(cmd, "edit.copy_clip_gain") == 0) {
        Time::Range r{ p.i64("start").value_or(0), p.i64("end").value_or(0) };
        clipboard.clipGain.clear();
        std::vector<std::pair<long long, float>> pts;
        for (auto& t : doc.tracks)
            for (const auto& c : t.clips)
                if (Time::Range::newRange(c.start, ClipOps::end(c)).overlaps(r))
                    pts.emplace_back(c.start - r.start, c.gainDb);
        clipboard.clipGain.push_back(pts);
        clipboard.length = r.len();
        return Result::num("copied", 1);
    }

    if (std::strcmp(cmd, "edit.paste_clip_gain") == 0) {
        const long long at = p.i64("at").value_or(0);
        if (!clipboard.clipGain.empty()) {
            for (auto& t : doc.tracks)
                for (auto& c : t.clips)
                    if (c.start >= at)
                        for (const auto& pt : clipboard.clipGain[0]) c.gainDb = pt.second;
        }
        return Result::num("pasted", 1);
    }

    if (std::strcmp(cmd, "edit.cut_clip_gain") == 0) {
        Time::Range r = doc.sessionEdit.selection;
        clipboard.clipGain.clear();
        std::vector<std::pair<long long, float>> pts;
        for (auto& t : doc.tracks)
            for (const auto& c : t.clips)
                if (Time::Range::newRange(c.start, ClipOps::end(c)).overlaps(r)) pts.emplace_back(c.start - r.start, c.gainDb);
        clipboard.clipGain.push_back(pts);
        for (auto& t : doc.tracks)
            for (auto& c : t.clips)
                if (Time::Range::newRange(c.start, ClipOps::end(c)).overlaps(r)) c.gainDb = 0;
        return Result::num("cut", 1);
    }

    if (std::strcmp(cmd, "edit.move_to_new_playlist") == 0) {
        Time::Range r = rangeParam(*this, p);
        for (uint64_t tn : trackNums(*this, p)) {
            Session::Track* tr = Session::findTrackByNum(doc, tn);
            if (!tr) continue;
            auto clips = EngEdit::copyRange(doc, tr->id, r);
            EngEdit::clearRange(doc, tr->id, r, false);
            Session::Playlist pl;
            pl.name = tr->name + ".01";
            pl.clips = std::move(clips);
            for (auto& c : pl.clips) {
                c.numId = Session::newClipNumId(doc);
                c.id = "clip_" + std::to_string(c.numId);
            }
            tr->playlists.push_back(std::move(pl));
        }
        return Result::num("tracks", static_cast<double>(trackNums(*this, p).size()));
    }

    if (std::strcmp(cmd, "track.playlist_select") == 0) {
        const size_t idx = static_cast<size_t>(p.numOr("index", 0));
        for (auto& t : doc.tracks) t.activePlaylist = idx;
        return Result::num("index", static_cast<double>(idx));
    }

    if (std::strcmp(cmd, "track.designate_target_playlist") == 0) {
        for (auto& t : doc.tracks) t.targetPlaylist = t.activePlaylist;
        return Result::num("target", 1);
    }

    if (std::strcmp(cmd, "edit.copy_to_target_playlist") == 0) {
        Time::Range r = rangeParam(*this, p);
        for (auto& tr : doc.tracks) {
            if (!tr.targetPlaylist || *tr.targetPlaylist == 0) return bad("target");
            auto clips = EngEdit::copyRange(doc, tr.id, r);
            size_t pi = *tr.targetPlaylist - 1;
            if (pi >= tr.playlists.size()) tr.playlists.resize(pi + 1);
            for (auto& c : clips) {
                c.numId = Session::newClipNumId(doc);
                c.id = "clip_" + std::to_string(c.numId);
                tr.playlists[pi].clips.push_back(c);
            }
        }
        return Result::num("tracks", 1);
    }

    if (std::strcmp(cmd, "edit.automation_coalesce_volume_to_clip_gain") == 0) {
        for (auto& t : doc.tracks) {
            const Session::AutomationLane* l = t.lane({Session::AutoParamKind::Volume, 0});
            for (auto& c : t.clips) {
                const long long mid = c.start + c.length / 2;
                c.gainDb = l ? l->valueAt(mid, t.gainDb) : t.gainDb;
            }
            t.automation.erase(std::remove_if(t.automation.begin(), t.automation.end(),
                                              [](const auto& x) { return x.param.kind == Session::AutoParamKind::Volume; }),
                               t.automation.end());
        }
        return Result::num("clips", 1);
    }

    if (std::strcmp(cmd, "edit.automation_coalesce_clip_gain_to_volume") == 0) {
        for (auto& t : doc.tracks) {
            Session::AutomationLane* l = t.laneMut({Session::AutoParamKind::Volume, 0});
            for (const auto& c : t.clips) l->setPoint(c.start, c.gainDb);
            for (auto& c : t.clips) c.gainDb = 0;
        }
        return Result::num("clips", 1);
    }

    if (std::strcmp(cmd, "mix.volume") == 0) {
        for (auto& t : doc.tracks) t.gainDb = static_cast<float>(p.numOr("db", t.gainDb));
        return Result::num("db", p.numOr("db", 0));
    }

    if (std::strcmp(cmd, "edit.automation_glide_to_all") == 0) {
        Time::Range r = rangeParam(*this, p);
        for (auto& t : doc.tracks) {
            Session::AutomationLane* l = t.laneMut({Session::AutoParamKind::Volume, 0});
            const float a = l->valueAt(r.start, t.gainDb);
            const float b = l->valueAt(r.end, t.gainDb);
            l->setPoint(r.start, a);
            l->setPoint(r.end, b);
        }
        return Result::num("glide", 1);
    }

    if (std::strcmp(cmd, "edit.automation_trim_to_all") == 0) {
        Time::Range r = rangeParam(*this, p);
        for (auto& t : doc.tracks)
            if (Session::AutomationLane* l = t.laneMut({Session::AutoParamKind::Volume, 0}))
                l->setPoint((r.start + r.end) / 2, 0.0f);
        return Result::num("trim", 1);
    }

    if (std::strcmp(cmd, "edit.paste_merge_midi") == 0) {
        const long long at = p.i64("at").value_or(0);
        for (uint64_t tn : trackNums(*this, p)) {
            Session::Track* tr = Session::findTrackByNum(doc, tn);
            if (!tr || tr->clips.empty() || !tr->clips[0].midi) continue;
            for (const auto& row : clipboard.tracks)
                for (const auto& c : row)
                    if (c.midi)
                        for (auto n : c.midi->notes) {
                            n.start += at - tr->clips[0].start;
                            tr->clips[0].midi->notes.push_back(n);
                        }
        }
        return Result::num("merged", 1);
    }

    if (std::strcmp(cmd, "edit.tce_to_timeline") == 0) {
        Time::Range target = doc.sessionEdit.timelineSelection;
        if (p.i64("start") && p.i64("end")) target = rangeParam(*this, p);
        const auto ids = clipIds(*this, p);
        if (ids.empty() || target.isEmpty()) return bad("selection");
        Session::Clip* c = clipMutNum(doc, ids[0]);
        if (!c) return bad("clip");
        auto pcm = readClipSource(doc, *c);
        const double ratio = static_cast<double>(target.len()) / std::max(1LL, c->length);
        auto stretched = resampleLinear(pcm, ratio);
        Session::Source* src = addRenderedSource(doc, "tce", std::move(stretched), 48000);
        c->sourceId = src->id;
        c->offset = 0;
        c->start = target.start;
        c->length = target.len();
        c->stretch = 1.0;
        return Result::num("ratio", ratio);
    }

    if (std::strcmp(cmd, "track.move_to_new_folder") == 0) {
        Session::Track folder;
        folder.numId = doc.nextTrackId++;
        folder.id = "folder_" + std::to_string(folder.numId);
        folder.name = "Folder";
        folder.kind = Session::TrackKind::Folder;
        folder.isFolder = true;
        doc.tracks.insert(doc.tracks.begin(), std::move(folder));
        for (size_t i = 1; i < doc.tracks.size(); ++i) doc.tracks[i].folderParent = doc.tracks[0].id;
        return Result::num("folder", static_cast<double>(doc.tracks[0].numId));
    }

    if (std::strcmp(cmd, "edit.extend_down_to_members") == 0 || std::strcmp(cmd, "edit.extend_up_to_members") == 0) {
        const bool down = std::strcmp(cmd, "edit.extend_down_to_members") == 0;
        std::vector<uint64_t> sel = doc.sessionEdit.selectedTracks;
        for (auto& t : doc.tracks)
            if (t.isFolder && std::find(sel.begin(), sel.end(), t.numId) != sel.end())
                for (auto& m : doc.tracks)
                    if (m.folderParent == t.id) sel.push_back(m.numId);
        if (!down)
            for (auto& t : doc.tracks)
                if (std::find(sel.begin(), sel.end(), t.numId) != sel.end() && !t.folderParent.empty())
                    if (Session::Track* p = Session::findTrack(doc, t.folderParent)) sel.push_back(p->numId);
        std::sort(sel.begin(), sel.end());
        sel.erase(std::unique(sel.begin(), sel.end()), sel.end());
        doc.sessionEdit.selectedTracks = sel;
        return Result::num("tracks", static_cast<double>(sel.size()));
    }

    if (std::strcmp(cmd, "edit.marker_lane_move_down") == 0 || std::strcmp(cmd, "edit.marker_lane_move_up") == 0 ||
        std::strcmp(cmd, "edit.marker_lane_extend_down") == 0 || std::strcmp(cmd, "edit.marker_lane_extend_up") == 0) {
        const bool extend = std::strstr(cmd, "extend") != nullptr;
        const bool up = std::strstr(cmd, "up") != nullptr;
        int top = static_cast<int>(doc.sessionEdit.values["selection.marker_lane.top"]);
        int bot = static_cast<int>(doc.sessionEdit.values["selection.marker_lane.bottom"]);
        if (top == 0) top = 1;
        if (bot == 0) bot = top;
        if (!extend && up) {
            top = std::max(1, top - 1);
            bot = top;
        } else if (!extend && !up) {
            bot = std::min(5, bot + 1);
            top = bot;
        } else if (extend && up)
            top = std::max(1, top - 1);
        else
            bot = std::min(5, bot + 1);
        doc.sessionEdit.values["selection.marker_lane.top"] = top;
        doc.sessionEdit.values["selection.marker_lane.bottom"] = bot;
        Result r;
        r.ok = true;
        r.arr = {static_cast<double>(top), static_cast<double>(bot)};
        r.nums["lanes"] = 1;
        return r;
    }

    if (std::strcmp(cmd, "edit.duplicate_extend") == 0) {
        Time::Range r = rangeParam(*this, p);
        int n = 0;
        for (uint64_t tn : trackNums(*this, p)) {
            if (Session::Track* t = Session::findTrackByNum(doc, tn)) {
                auto clips = EngEdit::copyRange(doc, t->id, r);
                auto out = EngEdit::paste(doc, t->id, r.end, clips, r.len(), false);
                if (!out.error) n += static_cast<int>(out.ids.size());
            }
        }
        doc.sessionEdit.selection = {r.start, r.end + r.len()};
        return Result::num("clips", n);
    }

    if (std::strcmp(cmd, "track.elastic") == 0) {
        if (Session::Track* t = Session::findTrackByNum(doc, p.u64("track").value_or(0))) t->elastic = true;
        return Result::num("elastic", 1);
    }

    if (std::strcmp(cmd, "clip.elastic_properties") == 0) {
        const double ratio = std::clamp(p.numOr("ratio", 1.0), 0.05, 20.0);
        int rendered = 0;
        for (uint64_t id : clipIds(*this, p)) {
            Session::Clip* c = clipMutNum(doc, id);
            if (!c) continue;
            auto pcm = readClipSource(doc, *c);
            const std::string key = "elastic." + std::to_string(id) + ".src";
            if (doc.sessionEdit.values.find(key) == doc.sessionEdit.values.end())
                doc.sessionEdit.values[key] = static_cast<double>(Session::findSource(doc, c->sourceId)->numId);
            auto out = resampleLinear(pcm, ratio);
            Session::Source* src = addRenderedSource(doc, "warp", std::move(out), 48000);
            c->sourceId = src->id;
            c->offset = 0;
            c->length = static_cast<long long>(std::llround(c->length * ratio));
            c->stretch = 1.0;
            ClipOps::clampFades(*c);
            ++rendered;
        }
        return Result::num("rendered", rendered);
    }

    if (std::strcmp(cmd, "clip.remove_warp") == 0) {
        int restored = 0;
        for (uint64_t id : clipIds(*this, p)) {
            Session::Clip* c = clipMutNum(doc, id);
            if (!c) continue;
            const std::string key = "elastic." + std::to_string(id) + ".src";
            auto it = doc.sessionEdit.values.find(key);
            if (it == doc.sessionEdit.values.end()) continue;
            const uint64_t srcNum = static_cast<uint64_t>(it->second);
            for (auto& s : doc.sources)
                if (s.numId == srcNum) {
                    c->sourceId = s.id;
                    c->stretch = 1.0;
                    restored++;
                    break;
                }
        }
        return Result::num("restored", restored);
    }

    if (std::strcmp(cmd, "clip.clear") == 0) {
        if (const auto* srcs = p.ids("sources")) {
            for (uint64_t sn : *srcs)
                doc.sources.erase(std::remove_if(doc.sources.begin(), doc.sources.end(),
                                                 [&](const Session::Source& s) {
                                                     return s.numId == sn || s.id == "src_" + std::to_string(sn);
                                                 }),
                                 doc.sources.end());
        }
        return Result::num("cleared", 1);
    }

    if (std::strcmp(cmd, "clip.identify_sync_point") == 0) {
        const long long at = p.i64("at").value_or(doc.sessionEdit.selection.start);
        for (uint64_t id : clipIds(*this, p)) {
            if (Session::Clip* c = clipMutNum(doc, id)) c->syncPoint = std::clamp(at - c->start, 0LL, c->length);
        }
        return Result::num("clips", static_cast<double>(clipIds(*this, p).size()));
    }

    if (std::strcmp(cmd, "clip.remove_sync_point") == 0) {
        int n = 0;
        for (uint64_t id : clipIds(*this, p)) {
            if (Session::Clip* c = clipMutNum(doc, id); c && c->syncPoint != 0) {
                c->syncPoint = 0;
                ++n;
            }
        }
        return Result::num("clips", n);
    }

    if (std::strcmp(cmd, "clip.effects_set") == 0) {
        const uint64_t cid = p.u64("clip").value_or(clipIds(*this, p).empty() ? 0 : clipIds(*this, p)[0]);
        auto it = p.objects.find("params");
        if (it != p.objects.end()) {
            for (const auto& kv : it->second) {
                double v = kv.second;
                if (kv.first == "gain") v = std::min(v, 24.0);
                doc.sessionEdit.values["clip_fx." + std::to_string(cid) + "." + kv.first] = v;
            }
        }
        return Result::num("clips", 1);
    }

    if (std::strcmp(cmd, "clip.effects_bypass") == 0) {
        const uint64_t cid = p.u64("clip").value_or(0);
        doc.sessionEdit.flags["clip_fx.bypass." + std::to_string(cid)] = true;
        return Result::num("bypass", 1);
    }

    if (std::strcmp(cmd, "edit.copy_clip_effects") == 0) {
        const uint64_t cid = p.u64("clip").value_or(0);
        clipboard.clipEffects.clear();
        const std::string prefix = "clip_fx." + std::to_string(cid) + ".";
        for (const auto& kv : doc.sessionEdit.values)
            if (kv.first.rfind(prefix, 0) == 0) clipboard.clipEffects.emplace_back(kv.first.substr(prefix.size()), kv.second);
        return Result::num("copied", 1);
    }

    if (std::strcmp(cmd, "edit.clear_clip_effects") == 0) {
        doc.sessionEdit.values.clear();
        return Result::num("cleared", 1);
    }

    if (std::strcmp(cmd, "edit.paste_clip_effects") == 0) {
        const uint64_t cid = p.u64("clip").value_or(0);
        for (const auto& kv : clipboard.clipEffects)
            doc.sessionEdit.values["clip_fx." + std::to_string(cid) + "." + kv.first] = kv.second;
        return Result::num("pasted", 1);
    }

    if (std::strcmp(cmd, "clip.group") == 0) {
        const uint64_t g = Session::allocId(doc);
        for (uint64_t id : clipIds(*this, p))
            if (Session::Clip* c = clipMutNum(doc, id)) c->group = g;
        return Result::num("group", static_cast<double>(g));
    }

    if (std::strcmp(cmd, "clip.ungroup") == 0) {
        for (uint64_t id : clipIds(*this, p))
            if (Session::Clip* c = clipMutNum(doc, id)) c->group.reset();
        return Result::num("ungroup", 1);
    }

    if (std::strcmp(cmd, "clip.regroup") == 0) {
        std::map<uint64_t, int> counts;
        for (uint64_t id : clipIds(*this, p))
            if (auto f = Session::findClipByNum(doc, id); f && f->second->group) counts[*f->second->group]++;
        uint64_t g = Session::allocId(doc);
        if (!counts.empty()) {
            auto it = std::max_element(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
            g = it->first;
        }
        for (uint64_t id : clipIds(*this, p))
            if (Session::Clip* c = clipMutNum(doc, id)) c->group = g;
        return Result::num("group", static_cast<double>(g));
    }

    if (std::strcmp(cmd, "clip.loop") == 0) {
        constexpr long long kMax = 1000;
        int made = 0;
        const bool fill = p.boolean("fill").value_or(false);
        for (uint64_t id : clipIds(*this, p)) {
            auto fc = Session::findClipByNum(doc, id);
            if (!fc) continue;
            Session::Clip& c = *fc->second;
            const long long len = c.length;
            long long reps = 0;
            long long last = len;
            if (fill) {
                long long end = Session::contentEnd(doc);
                for (const auto& o : Session::findTrack(doc, fc->first)->clips)
                    if (o.numId != id && o.start >= c.end()) end = std::min(end, o.start);
                const long long span = std::max(0LL, end - c.end());
                const long long whole = span / len;
                const long long rest = span % len;
                reps = whole + (rest > 0 ? 1 : 0);
                last = rest > 0 ? rest : len;
                if (reps > kMax) return Result::err("would need 1001 loops");
            } else if (p.i64("length")) {
                const long long total = *p.i64("length") - c.length;
                reps = total / len + (total % len ? 1 : 0);
                last = total % len ? total % len : len;
            } else {
                reps = static_cast<long long>(p.numOr("count", 4)) - 1;
                last = len;
            }
            if (!c.loopId) c.loopId = Session::allocId(doc);
            for (long long k = 1; k <= reps; ++k) {
                Session::Clip nc = c;
                nc.numId = Session::newClipNumId(doc);
                nc.id = "clip_" + std::to_string(nc.numId);
                nc.start = c.start + len * k;
                nc.loopId = c.loopId;
                if (k == reps && last < len) {
                    ClipOps::trimEndTo(nc, last);
                    nc.syncPoint = 0;
                }
                placeClip(doc, fc->first, nc);
                ++made;
            }
        }
        return Result::num("loops", made);
    }

    if (std::strcmp(cmd, "clip.unloop") == 0) {
        const bool remove = p.str("mode").value_or("remove") != "flatten";
        int copies = 0;
        for (uint64_t id : clipIds(*this, p)) {
            auto fc = Session::findClipByNum(doc, id);
            if (!fc) continue;
            auto chain = loopChain(doc, fc->first, id);
            if (chain.size() < 2) return bad("not a loop");
            Session::Track* tr = Session::findTrack(doc, fc->first);
            if (remove) {
                tr->clips.erase(std::remove_if(tr->clips.begin(), tr->clips.end(),
                                               [&](const Session::Clip& c) {
                                                   return c.numId != id && std::find(chain.begin(), chain.end(), c.numId) != chain.end();
                                               }),
                                tr->clips.end());
                copies += static_cast<int>(chain.size() - 1);
            } else {
                for (auto& c : tr->clips)
                    if (std::find(chain.begin(), chain.end(), c.numId) != chain.end()) {
                        c.loopId.reset();
                        c.group.reset();
                    }
                copies += static_cast<int>(chain.size() - 1);
            }
        }
        if (copies == 0) return bad("not a loop");
        return Result::num("copies", copies);
    }

    if (std::strcmp(cmd, "clip.conform_to_tempo") == 0) {
        for (uint64_t id : clipIds(*this, p)) {
            if (Session::Clip* c = clipMutNum(doc, id)) {
                if (p.has("source_bpm")) {
                    const double sb = p.numOr("source_bpm", 120.0);
                    const double ratio = sb / doc.time.tempoBpm;
                    c->stretch = ratio;
                    c->length = static_cast<long long>(c->length * ratio);
                } else if (doc.time.tempoBpm == 60.0) {
                    c->length *= 2;
                    c->stretch = 2.0;
                }
            }
        }
        return Result::num("conformed", 1);
    }

    if (std::strcmp(cmd, "event.tempo") == 0) {
        doc.time.tempoBpm = p.numOr("bpm", doc.time.tempoBpm);
        return Result::num("bpm", doc.time.tempoBpm);
    }

    if (std::strcmp(cmd, "clip.remove_pitch_shift") == 0) {
        int n = 0;
        for (uint64_t id : clipIds(*this, p)) {
            if (Session::Clip* c = clipMutNum(doc, id)) {
                auto pcm = readClipSource(doc, *c);
                Session::Source* src = addRenderedSource(doc, "pitch", std::move(pcm), 48000);
                c->sourceId = src->id;
                c->stretch = 1.0;
                ++n;
            }
        }
        return Result::num("clips", n);
    }

    if (std::strcmp(cmd, "clip.gain_render") == 0 || std::strcmp(cmd, "clip.effects_render") == 0) {
        for (uint64_t id : clipIds(*this, p)) {
            if (Session::Clip* c = clipMutNum(doc, id)) {
                auto pcm = readClipSource(doc, *c);
                const float g = std::pow(10.0f, c->gainDb / 20.0f);
                for (auto& v : pcm) v *= g;
                Session::Source* src = addRenderedSource(doc, "render", std::move(pcm), 48000);
                c->sourceId = src->id;
                c->gainDb = 0;
                c->stretch = 1.0;
            }
        }
        return Result::num("rendered", 1);
    }

    return Result::err(std::string("unknown command ") + cmd);
}

} // namespace EngCmd