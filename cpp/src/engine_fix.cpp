// Command engine for cmd/*.rs selftests (after SoundCraft crates/engine).
#include "engine.h"
#include "clip_ops.h"
#include "consolidate.h"
#include "engine_edit.h"
#include "import_audio.h"
#include "project.h"
#include "record_bounce.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <set>
#include <sstream>

namespace EngCmd {
namespace {

using EngEdit::placeClip;

Session::Clip* clipMutNum(Session::Document& d, uint64_t id) {
    if (auto f = Session::findClipByNum(d, id)) return f->second;
    return nullptr;
}

std::vector<Session::Clip>& plClips(Session::Track& t) { return t.activePlaylist == 0 ? t.clips : t.playlists[t.activePlaylist - 1].clips; }

std::vector<uint64_t> clipIds(const Engine& e, const Engine::Params& p) {
    if (const auto* v = p.ids("clips")) return *v;
    if (const auto* v = p.ids("clip")) return *v;
    if (auto u = p.u64("clip")) return {*u};
    return e.doc.sessionEdit.selectedClips;
}

std::vector<uint64_t> trackNums(const Engine& e, const Engine::Params& p) {
    if (const auto* v = p.ids("tracks")) return *v;
    if (const auto* v = p.ids("track")) return *v;
    if (auto u = p.u64("track")) return {*u};
    if (auto s = p.str("track")) {
        if (const Session::Track* t = Session::findTrackByName(e.doc, *s)) return {t->numId};
    }
    return e.doc.sessionEdit.selectedTracks;
}

Time::Range rangeParam(const Engine& e, const Engine::Params& p) {
    long long st = p.i64("start").value_or(e.doc.sessionEdit.selection.start);
    long long en = p.i64("end").value_or(e.doc.sessionEdit.selection.end);
    return Time::Range::newRange(st, en);
}

std::optional<long long> positionParam(const Engine::Params& p, const char* key) {
    if (auto v = p.i64(key)) return *v;
    if (auto v = p.num(key)) return static_cast<long long>(*v);
    return std::nullopt;
}

std::optional<Session::FadeShape> fadeShapeParam(const Engine::Params& p) {
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
    std::optional<long long> beforeEnd;
    std::optional<long long> afterStart;
    for (const auto& o : tr->clips) {
        if (o.numId == clipNum) continue;
        if (o.start < c.start) beforeEnd = std::max(beforeEnd.value_or(std::numeric_limits<long long>::min()), ClipOps::end(o));
        if (o.start > c.start) afterStart = std::min(afterStart.value_or(std::numeric_limits<long long>::max()), o.start);
    }
    if (beforeEnd) lo = std::min(*beforeEnd, c.start);
    if (afterStart) hi = std::max(*afterStart, ClipOps::end(c));
    return {frames, lo, hi};
}

Session::Source* addRenderedSource(Session::Document& d, const std::string& name, std::vector<float> pcm, int sr) {
    Session::Source src;
    src.numId = d.nextSourceId++;
    src.id = "src_" + std::to_string(src.numId);
    src.name = name;
    src.path = name;
    src.sampleRate = sr;
    src.channels = 1;
    src.frames = static_cast<long long>(pcm.size());
    Session::AudioBuffer buf;
    buf.sampleRate = static_cast<uint32_t>(sr);
    buf.channels = {pcm};
    d.pool.insert(src.numId, std::make_shared<Session::SourceAudio>(std::move(buf)));
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
    if (a.length == b.length && a.loopId && a.loopId == b.loopId) return true;
    if (a.isMidi != b.isMidi) return false;
    if (a.isMidi) return a.midi && b.midi && a.midi->notes == b.midi->notes;
    return a.sourceId == b.sourceId && a.offset == b.offset;
}

bool sameClipContent(const Session::Clip& a, const Session::Clip& b) {
    if (a.isMidi != b.isMidi) return false;
    if (a.isMidi) return a.midi && b.midi && a.midi->notes == b.midi->notes;
    return a.sourceId == b.sourceId && a.offset == b.offset;
}

bool cutShortLoop(const std::vector<Session::Clip>& clips, const Session::Clip& full, const Session::Clip& shortc) {
    auto wholeCopy = [&] {
        for (const auto& x : clips)
            if (x.numId != full.numId && ClipOps::end(x) == full.start && sameLoopMaterial(x, full)) return true;
        return false;
    };
    if (!full.loopId || *full.loopId != shortc.loopId) return false;
    if (shortc.length >= full.length) return false;
    for (const auto& x : clips)
        if (x.loopId && shortc.loopId && *x.loopId == *shortc.loopId && x.start == ClipOps::end(shortc)) return false;
    return wholeCopy() || sameClipContent(full, shortc);
}

std::vector<uint64_t> loopChain(Session::Document& s, const std::string& track, uint64_t id) {
    Session::Track* tr = Session::findTrack(s, track);
    if (!tr) return {};
    const auto& clips = tr->clips;
    const Session::Clip* cur = nullptr;
    for (const auto& c : clips)
        if (c.numId == id) {
            cur = &c;
            break;
        }
    if (!cur) return {};
    int guard = 0;
    while (guard++ < static_cast<int>(clips.size()) + 1) {
        bool found = false;
        for (const auto& x : clips) {
            if (x.numId == cur->numId) continue;
            if (ClipOps::end(x) == cur->start &&
                (sameLoopMaterial(x, *cur) || cutShortLoop(clips, x, *cur) ||
                 (x.loopId && cur->loopId && *x.loopId == *cur->loopId) ||
                 (!x.isMidi && !cur->isMidi && x.sourceId == cur->sourceId && x.offset == cur->offset))) {
                cur = &x;
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    std::vector<uint64_t> chain = {cur->numId};
    while (guard++ < static_cast<int>(clips.size()) + 1) {
        const Session::Clip* next = nullptr;
        for (const auto& x : clips) {
            if (x.numId == cur->numId) continue;
            if (std::find(chain.begin(), chain.end(), x.numId) != chain.end()) continue;
            if (x.start == ClipOps::end(*cur) &&
                (sameLoopMaterial(x, *cur) || (x.loopId && cur->loopId && *x.loopId == *cur->loopId))) {
                next = &x;
                break;
            }
        }
        if (!next) break;
        chain.push_back(next->numId);
        cur = next;
    }
    for (const auto& x : clips) {
        if (std::find(chain.begin(), chain.end(), x.numId) != chain.end()) continue;
        if (x.start == ClipOps::end(*cur) && cutShortLoop(clips, *cur, x)) chain.push_back(x.numId);
    }
    return chain;
}

static void setClipGainEnv(Session::Clip& c, std::vector<std::pair<long long, float>> pts) {
    const long long clipLen = c.length;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
                             [clipLen](const auto& pt) { return pt.first < 0 || pt.first > clipLen || !std::isfinite(pt.second); }),
              pts.end());
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), pts.end());
    for (auto& pt : pts) pt.second = std::clamp(pt.second, -144.0f, 36.0f);
    c.gainDb = 0.0f;
    const bool flat =
        pts.size() <= 1 ||
        std::all_of(pts.begin() + 1, pts.end(), [&](const auto& pt) { return std::abs(pt.second - pts.front().second) < 1e-6f; });
    if (flat) {
        c.gainDb = pts.empty() ? 0.0f : pts.front().second;
        c.gainEnv.clear();
    } else {
        c.gainEnv = std::move(pts);
    }
}


std::optional<std::tuple<std::string, long long, long long>> warpOriginal(Session::Document& d, uint64_t clipId) {
    const std::string base = "elastic." + std::to_string(clipId) + ".";
    auto itSrc = d.sessionEdit.values.find(base + "src");
    auto itOff = d.sessionEdit.values.find(base + "off");
    auto itLen = d.sessionEdit.values.find(base + "len");
    if (itSrc == d.sessionEdit.values.end() || itOff == d.sessionEdit.values.end() || itLen == d.sessionEdit.values.end()) return std::nullopt;
    const uint64_t srcNum = static_cast<uint64_t>(itSrc->second);
    for (auto& s : d.sources)
        if (s.numId == srcNum || s.id == "src_" + std::to_string(srcNum)) return std::tuple<std::string, long long, long long>{s.id, static_cast<long long>(itOff->second), static_cast<long long>(itLen->second)};
    return std::nullopt;
}

float clipDb(const Session::Clip& c, long long rel) {
    float db = c.gainDb;
    if (!c.gainEnv.empty()) db += ClipOps::envDbAt(c, rel);
    return db;
}

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
    return nums.count(k) || strs.count(k) || bools.count(k) || idLists.count(k) || numLists.count(k) || strLists.count(k);
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

void Engine::pushUndo(const Session::Document& snapshot, const char* label) {
    if (undoStack_.size() >= 80) undoStack_.erase(undoStack_.begin());
    undoStack_.push_back(snapshot);
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

// after SoundCraft (crates/engine/src/lib.rs MAX_OFFERED_IMPORTS, offer_import, drop_offered_imports, take_import)
constexpr size_t kMaxOfferedImports = 4;

void Engine::offerImport(ImportAudio::DecodedImport d) {
    imports_.erase(std::remove_if(imports_.begin(), imports_.end(),
                                  [&](const std::shared_ptr<ImportAudio::DecodedImport>& x) { return x && x->key == d.key; }),
                   imports_.end());
    imports_.push_back(std::make_shared<ImportAudio::DecodedImport>(std::move(d)));
    if (imports_.size() > kMaxOfferedImports) {
        imports_.erase(imports_.begin(), imports_.begin() + static_cast<std::ptrdiff_t>(imports_.size() - kMaxOfferedImports));
    }
}

void Engine::dropOfferedImports() {
    imports_.clear();
}

std::optional<ImportAudio::DecodedImport> Engine::takeImport(const std::filesystem::path& path) {
    const auto key = ImportAudio::fileKey(path);
    const uint32_t rate = static_cast<uint32_t>(std::max(1.0, doc.sampleRate));
    for (auto it = imports_.begin(); it != imports_.end(); ++it) {
        if (*it && (*it)->key == key && (*it)->rate == rate) {
            ImportAudio::DecodedImport out = std::move(**it);
            imports_.erase(it);
            return out;
        }
    }
    return std::nullopt;
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
        for (auto& c : t.clips) {
            for (const auto& s : src->clips) {
                if (s.id == c.id) {
                    const std::string id = c.id;
                    c = s;
                    c.id = id;
                    break;
                }
            }
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

Result Engine::execute(const char* cmd, const Engine::Params& p) {
    if (std::strcmp(cmd, "edit.undo") == 0) {
        if (!undo()) return Result::err("nothing to undo");
        Result r;
        r.ok = true;
        return r;
    }
    if (std::strcmp(cmd, "edit.redo") == 0) {
        if (!redo()) return Result::err("nothing to redo");
        Result r;
        r.ok = true;
        return r;
    }
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
    const bool changed = !Session::documentsEqual(doc, before);
    if (changed) {
        ++revision;
        ++touch;
    }
    const bool noundo = std::strcmp(cmd, "edit.restore_last_selection") == 0 || std::strcmp(cmd, "edit.copy") == 0 ||
                        std::strcmp(cmd, "edit.select") == 0 || std::strcmp(cmd, "edit.select_none") == 0;
    const char* label = std::strcmp(cmd, "clip.clear") == 0 ? "Clear Clips" : cmd;
    if (!noundo && changed) pushUndo(before, label);
    return r;
}

Result Engine::dispatch(const char* cmd, const Engine::Params& p) {
    const auto bad = [&](const std::string& msg) { return Result::err(msg); };

    if (std::strcmp(cmd, "track.new") == 0) {
        const int count = static_cast<int>(p.numOr("count", 1));
        Session::TrackKind kind = Session::TrackKind::Audio;
        if (auto k = p.str("kind")) {
            if (auto parsed = Session::trackKindFromId(*k)) kind = *parsed;
        }
        Session::ChannelFormat fmt = Session::ChannelFormat::Mono;
        if (auto f = p.str("format")) {
            if (auto parsed = Session::channelFormatFromId(*f)) fmt = *parsed;
        }
        std::optional<std::string> baseName = p.str("name");
        std::vector<double> ids;
        for (int i = 0; i < count; ++i) {
            const uint64_t id = Session::addTrack(doc, kind, fmt, baseName);
            ids.push_back(static_cast<double>(id));
        }
        Result r;
        r.ok = true;
        r.arr = std::move(ids);
        return r;
    }

    // after SoundCraft (crates/engine/src/cmd/file.rs import_audio, import_audio_target)
    if (std::strcmp(cmd, "file.import_audio") == 0) {
        std::vector<std::string> paths;
        if (auto it = p.strLists.find("paths"); it != p.strLists.end()) {
            paths = it->second;
        }
        if (auto one = p.str("path")) {
            paths.push_back(*one);
        }
        if (paths.empty()) {
            return bad("file.import_audio: `path` or `paths` required");
        }
        const long long at = std::max(0LL, positionParam(p, "at").value_or(0LL));
        const bool clipsListOnly = p.boolean("clips_list_only").value_or(false);
        const bool copyToFolder = p.boolean("copy_to_audio_files").value_or(false);

        std::optional<uint64_t> target;
        if (!clipsListOnly) {
            if (p.has("track")) {
                const auto trs = trackNums(*this, p);
                if (trs.empty()) return bad("file.import_audio: unknown track");
                target = trs[0];
            } else if (!p.boolean("new_tracks").value_or(true)) {
                for (uint64_t tid : doc.sessionEdit.selectedTracks) {
                    const Session::Track* t = Session::findTrackByNum(doc, tid);
                    if (t && t->kind == Session::TrackKind::Audio) {
                        target = t->numId;
                        break;
                    }
                }
                if (!target) {
                    return bad("file.import_audio: `new_tracks: false` needs `track` or a selected audio track");
                }
            }
        }

        Result out;
        out.ok = true;
        for (const std::string& rawPath : paths) {
            const std::filesystem::path fp = std::filesystem::u8path(rawPath);
            std::string name = fp.filename().u8string();
            if (name.empty()) name = "Audio";
            const std::string storedPath = copyToFolder ? ImportAudio::copyToProjectAudioFiles(doc, rawPath) : rawPath;
            Result r;
            if (auto offered = takeImport(fp)) {
                r = ImportAudio::placeDecoded(doc, name, *offered, storedPath, target, at, clipsListOnly);
            } else {
                std::error_code ec;
                if (!std::filesystem::is_regular_file(fp, ec)) {
                    return Result::err(rawPath + ": file not found");
                }
                std::ifstream f(fp, std::ios::binary | std::ios::ate);
                if (!f) return Result::err(rawPath + ": could not open file");
                const auto sz = f.tellg();
                if (sz <= 0) return Result::err(rawPath + ": empty file");
                std::vector<uint8_t> bytes(static_cast<size_t>(sz));
                f.seekg(0, std::ios::beg);
                if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
                    return Result::err(rawPath + ": read failed");
                }
                r = ImportAudio::importAudioBytes(doc, name, bytes.data(), bytes.size(), storedPath, target, at, clipsListOnly);
            }
            if (!r.ok) return r;
            out.nums = r.nums;
            out.arr.push_back(r.nums["frames"]);
        }
        return out;
    }

    // after SoundCraft (crates/engine/src/cmd/clip.rs clip.place_source)
    if (std::strcmp(cmd, "clip.place_source") == 0) {
        const auto srcNumOpt = p.u64("source");
        if (!srcNumOpt) return bad("clip.place_source: `source` id required");
        const uint64_t srcNum = *srcNumOpt;
        const long long atRaw = std::max(0LL, positionParam(p, "at").value_or(doc.sessionEdit.selection.start));
        std::optional<uint64_t> trackNum;
        if (p.has("track")) {
            const auto trs = trackNums(*this, p);
            if (trs.empty()) return bad("clip.place_source: unknown track");
            trackNum = trs[0];
        }
        const Session::Source* src = Session::findSourceByNum(doc, srcNum);
        if (!src) return bad("clip.place_source: no such audio file");
        const std::string name = src->name.empty() ? src->path : src->name;
        const long long frames = src->frames;
        const size_t chans = static_cast<size_t>(std::max(1, src->channels));
        const std::string srcId = src->id;

        uint64_t tNum = 0;
        if (trackNum) {
            const Session::Track* tr = Session::findTrackByNum(doc, *trackNum);
            if (!tr || tr->kind != Session::TrackKind::Audio) {
                return bad("clip.place_source: audio files go on audio tracks");
            }
            tNum = *trackNum;
        } else {
            tNum = Session::addTrack(doc, Session::TrackKind::Audio, Session::channelFormatForChannels(std::min<size_t>(chans, 2)), name);
        }
        Session::Track* tr = Session::findTrackByNum(doc, tNum);
        if (!tr) return bad("clip.place_source: target track not found");
        const long long len = std::max(1LL, frames);
        const bool sh = doc.sessionEdit.flags.count("mode.shuffle") && doc.sessionEdit.flags.at("mode.shuffle");
        const long long at = sh ? EngEdit::shuffleSlot(doc, tr->id, atRaw, {}) : atRaw;
        if (sh) {
            if (const char* why = EngEdit::makeRoom(doc, tr->id, at, len)) return Result::err(why);
        }
        Session::Clip clip;
        clip.numId = Session::newClipNumId(doc);
        clip.id = "clip_" + std::to_string(clip.numId);
        clip.name = name;
        clip.sourceId = srcId;
        clip.offset = 0;
        clip.start = at;
        clip.length = len;
        EngEdit::placeClip(doc, tr->id, clip);
        Result r;
        r.ok = true;
        r.nums["clip"] = static_cast<double>(clip.numId);
        r.nums["track"] = static_cast<double>(tNum);
        return r;
    }

    if (std::strcmp(cmd, "edit.select") == 0) {
        const bool hasTracks = p.has("tracks") || p.has("track");
        const auto tracks = trackNums(*this, p);
        const bool hasStart = p.has("start");
        const bool hasEnd = p.has("end");
        const auto* clips = p.ids("clips");
        const auto* sources = p.ids("sources");
        const bool listChanges = (clips != nullptr) || (sources != nullptr) || hasStart;
        if (hasTracks && !tracks.empty()) doc.sessionEdit.selectedTracks = tracks;
        if (hasStart && hasEnd) {
            doc.sessionEdit.selection = rangeParam(*this, p);
        } else if (hasStart) {
            const long long st = std::max(0LL, p.i64("start").value_or(0LL));
            doc.sessionEdit.selection = Time::Range::point(st);
        }
        if (clips) {
            std::set<uint64_t> knownClips;
            for (const auto& t : doc.tracks) {
                for (const auto& c : t.clips) knownClips.insert(c.numId);
            }
            doc.sessionEdit.selectedClips.clear();
            for (uint64_t id : *clips) {
                if (knownClips.erase(id) > 0) doc.sessionEdit.selectedClips.push_back(id);
            }
        } else if (listChanges) {
            doc.sessionEdit.selectedClips.clear();
        }
        if (listChanges) {
            std::set<uint64_t> knownSources;
            for (const auto& f : doc.sources) knownSources.insert(f.numId);
            doc.sessionEdit.selectedSources.clear();
            if (sources) {
                for (uint64_t id : *sources) {
                    if (knownSources.erase(id) > 0) doc.sessionEdit.selectedSources.push_back(id);
                }
            }
        }
        Result r;
        r.ok = true;
        r.nums["tracks"] = static_cast<double>(doc.sessionEdit.selectedTracks.size());
        r.nums["clips"] = static_cast<double>(doc.sessionEdit.selectedClips.size());
        r.nums["sources"] = static_cast<double>(doc.sessionEdit.selectedSources.size());
        return r;
    }

    if (std::strcmp(cmd, "edit.select_none") == 0) {
        doc.sessionEdit.selectedTracks.clear();
        doc.sessionEdit.selectedClips.clear();
        doc.sessionEdit.selectedSources.clear();
        doc.sessionEdit.selection.end = doc.sessionEdit.selection.start;
        return Result::num("clips", 0);
    }

    if (std::strcmp(cmd, "edit.fades_create") == 0) {
        const bool targeted = p.has("side") || p.ids("clip") || p.u64("clip");
        if (targeted) {
            if (p.has("tracks") || p.has("start") || p.has("end"))
                return bad("clip fades cannot also specify tracks or an edit range");
            const auto ids = clipIds(*this, p);
            if (ids.size() != 1) return bad("clip");
            const uint64_t cid = ids[0];
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
        std::string err;
        for (uint64_t tn : trackNums(*this, p)) {
            Session::Track* t = Session::findTrackByNum(doc, tn);
            if (!t) continue;
            if (Session::isMidiKind(t->kind)) {
                if (EngEdit::consolidateMidi(doc, t->id, r)) ++n;
            } else if (Consolidate::track(doc, t->id, r, err)) {
                ++n;
            }
        }
        if (!err.empty() && n == 0) return bad(err.c_str());
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
        Engine::Params q = p;
        q.nums["delta_db"] = -1.0;
        for (uint64_t id : clipIds(*this, q))
            if (Session::Clip* c = clipMutNum(doc, id)) c->gainDb = std::clamp(c->gainDb - 1.0f, -144.0f, 36.0f);
        return Result::num("clips", static_cast<double>(clipIds(*this, q).size()));
    }

    if (std::strcmp(cmd, "edit.copy_clip_gain") == 0) {
        Time::Range r = rangeParam(*this, p);
        if (r.isEmpty()) return bad("range");
        clipboard.clipGain.clear();
        for (uint64_t tn : trackNums(*this, p)) {
            const Session::Track* tr = Session::findTrackByNum(doc, tn);
            if (!tr) continue;
            std::vector<std::pair<long long, float>> env;
            for (const auto& c : tr->clips) {
                auto cr = ClipOps::range(c);
                if (!cr.overlaps(r)) continue;
                const long long i0 = std::max(r.start, c.start);
                const long long i1 = std::min(r.end, ClipOps::end(c));
                env.emplace_back(i0 - r.start, clipDb(c, i0 - c.start));
                for (const auto& pt : c.gainEnv) {
                    const long long at = c.start + pt.first;
                    if (at > i0 && at < i1 - 1) env.emplace_back(at - r.start, clipDb(c, pt.first));
                }
                env.emplace_back(i1 - 1 - r.start, clipDb(c, i1 - 1 - c.start));
            }
            std::sort(env.begin(), env.end());
            env.erase(std::unique(env.begin(), env.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), env.end());
            clipboard.clipGain.push_back(std::move(env));
        }
        clipboard.length = r.len();
        return Result::num("copied", 1);
    }

    if (std::strcmp(cmd, "edit.paste_clip_gain") == 0) {
        if (clipboard.clipGain.empty()) return bad("no clip gain has been copied");
        const long long at = p.i64("at").value_or(doc.sessionEdit.selection.start);
        const Time::Range paste = Time::Range::newRange(at, at + std::max(clipboard.length, 1LL));
        int n = 0;
        const auto tracks = trackNums(*this, p);
        for (size_t i = 0; i < tracks.size(); ++i) {
            Session::Track* tr = Session::findTrackByNum(doc, tracks[i]);
            if (!tr) continue;
            const auto& env = clipboard.clipGain[i % clipboard.clipGain.size()];
            for (auto& c : tr->clips) {
                if (!ClipOps::range(c).overlaps(paste)) continue;
                const long long a = std::max(paste.start, c.start) - c.start;
                const long long b = std::min(paste.end, ClipOps::end(c)) - c.start;
                std::vector<std::pair<long long, float>> pts;
                for (const auto& pt : c.gainEnv) pts.emplace_back(pt.first, clipDb(c, pt.first));
                pts.emplace_back(0, clipDb(c, 0));
                pts.emplace_back(c.length, clipDb(c, c.length));
                std::vector<std::pair<long long, float>> out;
                for (const auto& pt : pts)
                    if (pt.first < a || pt.first > b) out.push_back(pt);
                if (a > 0) out.emplace_back(a - 1, clipDb(c, a - 1));
                if (b < c.length) out.emplace_back(b, clipDb(c, b));
                for (const auto& pt : env) {
                    const long long rel = at + pt.first - c.start;
                    if (rel >= a && rel < b) out.emplace_back(rel, pt.second);
                }
                setClipGainEnv(c, std::move(out));
                ++n;
            }
        }
        return Result::num("pasted", n);
    }

    if (std::strcmp(cmd, "edit.cut_clip_gain") == 0) {
        Result r = dispatch("edit.copy_clip_gain", p);
        if (!r.ok) return r;
        Time::Range sel = rangeParam(*this, p);
        for (uint64_t tn : trackNums(*this, p)) {
            Session::Track* tr = Session::findTrackByNum(doc, tn);
            if (!tr) continue;
            for (auto& c : tr->clips) {
                if (!ClipOps::range(c).overlaps(sel)) continue;
                c.gainDb = 0;
                c.gainEnv.clear();
            }
        }
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
                c.start += r.start;
            }
            tr->playlists.push_back(std::move(pl));
        }
        return Result::num("tracks", static_cast<double>(trackNums(*this, p).size()));
    }

    if (std::strcmp(cmd, "track.playlist_new") == 0 || std::strcmp(cmd, "track.playlist_duplicate") == 0) {
        const bool dup = std::strcmp(cmd, "track.playlist_duplicate") == 0;
        const auto trs = trackNums(*this, p);
        if (trs.empty()) return bad("track.playlist_new: no tracks selected");
        for (uint64_t tn : trs) {
            Session::Track* tr = Session::findTrackByNum(doc, tn);
            if (!tr) continue;
            std::vector<Session::Clip> clips;
            if (dup) clips = tr->activeClips();
            for (auto& c : clips) {
                c.numId = Session::newClipNumId(doc);
                c.id = "clip_" + std::to_string(c.numId);
            }
            Session::Playlist pl;
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s.%02zu", tr->name.c_str(), tr->playlists.size() + 1);
            pl.name = p.str("name").value_or(nameBuf);
            pl.clips = std::move(clips);
            tr->playlists.push_back(std::move(pl));
            tr->activePlaylist = tr->playlists.size();
        }
        return Result::num("tracks", static_cast<double>(trs.size()));
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
            if (tr.activePlaylist == *tr.targetPlaylist) return bad("target");
            auto clips = EngEdit::copyRange(doc, tr.id, r);
            size_t pi = *tr.targetPlaylist - 1;
            if (pi >= tr.playlists.size()) tr.playlists.resize(pi + 1);
            for (auto& c : clips) {
                c.numId = Session::newClipNumId(doc);
                c.id = "clip_" + std::to_string(c.numId);
                c.start += r.start;
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
        for (uint64_t tn : trackNums(*this, p)) if (Session::Track* t = Session::findTrackByNum(doc, tn)) t->elastic = true;
        return Result::num("elastic", 1);
    }

    if (std::strcmp(cmd, "clip.elastic_properties") == 0) {
        const double ratio = std::clamp(p.numOr("ratio", 1.0), 0.05, 20.0);
        int rendered = 0;
        for (uint64_t id : clipIds(*this, p)) {
            Session::Clip* c = clipMutNum(doc, id);
            if (!c) continue;
            Session::Clip base = *c;
            if (auto orig = warpOriginal(doc, id)) {
                base.sourceId = std::get<0>(*orig);
                base.offset = std::get<1>(*orig);
                base.length = std::get<2>(*orig);
                base.stretch = 1.0;
            }
            const Session::Track* tr = nullptr;
            for (auto& t : doc.tracks)
                for (const auto& cl : t.clips)
                    if (cl.numId == id) {
                        tr = &t;
                        break;
                    }
            if (tr && !tr->elastic) {
                c->stretch = static_cast<float>(ratio);
                continue;
            }
            auto pcm = readClipSource(doc, base);
            const std::string prefix = "elastic." + std::to_string(id) + ".";
            if (doc.sessionEdit.values.find(prefix + "src") == doc.sessionEdit.values.end()) {
                const Session::Source* src = Session::findSource(doc, base.sourceId);
                if (src) {
                    doc.sessionEdit.values[prefix + "src"] = static_cast<double>(src->numId);
                    doc.sessionEdit.values[prefix + "off"] = static_cast<double>(base.offset);
                    doc.sessionEdit.values[prefix + "len"] = static_cast<double>(base.length);
                }
            }
            doc.sessionEdit.values[prefix + "ratio"] = ratio;
            const long long outLen = std::max(1LL, static_cast<long long>(std::llround(base.length * ratio)));
            auto out = resampleLinear(pcm, ratio);
            Session::Source* src = addRenderedSource(doc, "warp", std::move(out), 48000);
            c->sourceId = src->id;
            c->offset = 0;
            c->length = outLen;
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
            c->stretch = 1.0;
            if (auto orig = warpOriginal(doc, id)) {
                c->sourceId = std::get<0>(*orig);
                c->offset = std::get<1>(*orig);
                c->length = std::max(1LL, std::get<2>(*orig));
                ++restored;
            }
        }
        return Result::num("restored", restored);
    }

    // after SoundCraft (crates/engine/src/cmd/clip_list.rs clear)
    if (std::strcmp(cmd, "clip.clear") == 0) {
        if ((p.strs.count("clips") || p.nums.count("clips") || p.bools.count("clips")) ||
            (p.strs.count("sources") || p.nums.count("sources") || p.bools.count("sources"))) {
            return bad("clip.clear: `clips` and `sources` must be arrays of ids");
        }
        const auto* clipsParam = p.ids("clips");
        const auto* sourcesParam = p.ids("sources");
        std::set<uint64_t> clips;
        std::set<uint64_t> sources;
        if (!clipsParam && !sourcesParam) {
            clips.insert(doc.sessionEdit.selectedClips.begin(), doc.sessionEdit.selectedClips.end());
            sources.insert(doc.sessionEdit.selectedSources.begin(), doc.sessionEdit.selectedSources.end());
        } else {
            if (clipsParam) clips.insert(clipsParam->begin(), clipsParam->end());
            if (sourcesParam) sources.insert(sourcesParam->begin(), sourcesParam->end());
        }

        std::set<uint64_t> knownClips;
        for (const auto& t : doc.tracks) {
            for (const auto& c : t.clips) knownClips.insert(c.numId);
            for (const auto& pl : t.playlists) {
                for (const auto& c : pl.clips) knownClips.insert(c.numId);
            }
        }
        for (uint64_t cid : clips) {
            if (!knownClips.count(cid)) {
                return Result::err("clip.clear: no clip with id " + std::to_string(cid));
            }
        }
        std::vector<const Session::Source*> files;
        for (uint64_t fid : sources) {
            const Session::Source* f = Session::findSourceByNum(doc, fid);
            if (!f) {
                return Result::err("clip.clear: no audio file with id " + std::to_string(fid));
            }
            files.push_back(f);
        }
        if (clips.empty() && files.empty()) {
            return bad("clip.clear: nothing to clear: pass `clips` or `sources`, or select them in the Clip List");
        }
        const bool withClips = p.boolean("with_clips").value_or(false);
        if (!withClips) {
            for (const Session::Source* f : files) {
                const auto usingClips = Session::clipsUsing(doc, f->numId);
                size_t n = 0;
                for (const Session::Clip* c : usingClips) {
                    if (!clips.count(c->numId)) ++n;
                }
                if (n > 0) {
                    const std::string plural = (n == 1) ? "" : "s";
                    const std::string fname = f->name.empty() ? f->path : f->name;
                    return Result::err("clip.clear: `" + fname + "` is used by " + std::to_string(n) + " clip" +
                                       plural + " on the timeline; pass `with_clips: true` to remove them too");
                }
            }
        }
        std::set<uint64_t> gone = clips;
        for (uint64_t fid : sources) {
            for (const Session::Clip* c : Session::clipsUsing(doc, fid)) {
                gone.insert(c->numId);
            }
        }
        for (auto& t : doc.tracks) {
            t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                         [&](const Session::Clip& c) { return gone.count(c.numId) > 0; }),
                          t.clips.end());
            for (auto& pl : t.playlists) {
                pl.clips.erase(std::remove_if(pl.clips.begin(), pl.clips.end(),
                                              [&](const Session::Clip& c) { return gone.count(c.numId) > 0; }),
                               pl.clips.end());
            }
        }
        doc.sources.erase(std::remove_if(doc.sources.begin(), doc.sources.end(),
                                         [&](const Session::Source& f) { return sources.count(f.numId) > 0; }),
                          doc.sources.end());
        for (uint64_t fid : sources) {
            doc.pool.remove(fid);
        }
        doc.sessionEdit.selectedClips.erase(
            std::remove_if(doc.sessionEdit.selectedClips.begin(), doc.sessionEdit.selectedClips.end(),
                           [&](uint64_t id) { return gone.count(id) > 0; }),
            doc.sessionEdit.selectedClips.end());
        doc.sessionEdit.selectedSources.erase(
            std::remove_if(doc.sessionEdit.selectedSources.begin(), doc.sessionEdit.selectedSources.end(),
                           [&](uint64_t id) { return sources.count(id) > 0; }),
            doc.sessionEdit.selectedSources.end());
        Result r;
        r.ok = true;
        r.nums["clips"] = static_cast<double>(gone.size());
        r.nums["sources"] = static_cast<double>(sources.size());
        r.nums["cleared"] = static_cast<double>(gone.size() + sources.size());
        return r;
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
        const auto fxIds = clipIds(*this, p);
        if (fxIds.empty()) return bad("clip");
        const uint64_t cid = fxIds[0];
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
        const auto ids = clipIds(*this, p);
        if (ids.empty()) return bad("select a clip");
        for (uint64_t cid : ids) doc.sessionEdit.flags["clip_fx.bypass." + std::to_string(cid)] = true;
        return Result::num("bypass", static_cast<double>(ids.size()));
    }

    if (std::strcmp(cmd, "edit.copy_clip_effects") == 0) {
        const auto ids = clipIds(*this, p);
        if (ids.empty()) return bad("select a clip");
        const uint64_t cid = ids[0];
        clipboard.clipEffects.clear();
        const std::string prefix = "clip_fx." + std::to_string(cid) + ".";
        for (const auto& kv : doc.sessionEdit.values)
            if (kv.first.rfind(prefix, 0) == 0) clipboard.clipEffects.emplace_back(kv.first.substr(prefix.size()), kv.second);
        if (clipboard.clipEffects.empty()) return bad("the clip has no clip effects");
        return Result::num("copied", static_cast<double>(clipboard.clipEffects.size()));
    }

    if (std::strcmp(cmd, "edit.clear_clip_effects") == 0) {
        const auto ids = clipIds(*this, p);
        int removed = 0;
        for (uint64_t cid : ids) {
            const std::string prefix = "clip_fx." + std::to_string(cid) + ".";
            const size_t before = doc.sessionEdit.values.size();
            for (auto it = doc.sessionEdit.values.begin(); it != doc.sessionEdit.values.end();) {
                if (it->first.rfind(prefix, 0) == 0)
                    it = doc.sessionEdit.values.erase(it);
                else
                    ++it;
            }
            removed += static_cast<int>(before - doc.sessionEdit.values.size());
            doc.sessionEdit.flags["clip_fx.bypass." + std::to_string(cid)] = false;
        }
        return Result::num("cleared", removed);
    }

    if (std::strcmp(cmd, "edit.paste_clip_effects") == 0) {
        const auto ids = clipIds(*this, p);
        if (ids.empty()) return bad("select a clip");
        for (uint64_t cid : ids) {
            const std::string prefix = "clip_fx." + std::to_string(cid) + ".";
            for (auto it = doc.sessionEdit.values.begin(); it != doc.sessionEdit.values.end();) {
                if (it->first.rfind(prefix, 0) == 0)
                    it = doc.sessionEdit.values.erase(it);
                else
                    ++it;
            }
            for (const auto& kv : clipboard.clipEffects)
                doc.sessionEdit.values[prefix + kv.first] = kv.second;
        }
        return Result::num("pasted", static_cast<double>(ids.size()));
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
            if (c.length <= 0) continue;
            const long long len = c.length;
            long long reps = 0;
            long long last = len;
            std::optional<long long> endPos;
            if (fill) {
                long long end = Session::contentEnd(doc);
                for (const auto& o : Session::findTrack(doc, fc->first)->clips)
                    if (o.numId != id && o.start >= c.end()) end = std::min(end, o.start);
                endPos = end;
            } else if (auto lp = p.i64("length")) {
                endPos = c.start + *lp;
            }
            if (endPos) {
                const long long span = std::max(0LL, *endPos - c.end());
                const long long whole = span / len;
                const long long rest = span % len;
                reps = whole + (rest > 0 ? 1 : 0);
                last = rest > 0 ? rest : len;
                if (reps > kMax) return Result::err("would need 1001 loops");
            } else {
                const long long count = static_cast<long long>(p.numOr("count", 4));
                reps = std::clamp(count - 1, 0LL, kMax);
                last = len;
            }
            if (reps > 0 && !c.loopId) {
                c.loopId = Session::allocId(doc);
            }
            for (long long k = 1; k <= reps; ++k) {
                Session::Clip nc = c;
                nc.numId = Session::newClipNumId(doc);
                nc.id = "clip_" + std::to_string(nc.numId);
                nc.start = c.start + len * k;
                nc.loopId = c.loopId;
                if (k == reps && last < len) {
                    ClipOps::trimEndTo(nc, nc.start + last);
                    nc.syncPoint = 0;
                    ClipOps::clampFades(nc);
                }
                placeClip(doc, fc->first, nc);
                ++made;
            }
        }
        return Result::num("loops", made);
    }

    if (std::strcmp(cmd, "clip.unloop") == 0) {
        const bool remove = p.str("mode").value_or("remove") != "flatten";
        std::vector<uint64_t> done;
        int copies = 0;
        for (uint64_t id : clipIds(*this, p)) {
            if (std::find(done.begin(), done.end(), id) != done.end()) continue;
            auto fc = Session::findClipByNum(doc, id);
            if (!fc) continue;
            auto chain = loopChain(doc, fc->first, id);
            done.insert(done.end(), chain.begin(), chain.end());
            if (chain.size() < 2) continue;
            Session::Track* tr = Session::findTrack(doc, fc->first);
            if (remove) {
                const std::vector<uint64_t> copies(chain.begin() + (chain.empty() ? 0 : 1), chain.end());
                tr->clips.erase(std::remove_if(tr->clips.begin(), tr->clips.end(),
                                               [&](const Session::Clip& c) {
                                                   return std::find(copies.begin(), copies.end(), c.numId) != copies.end();
                                               }),
                                tr->clips.end());
                doc.sessionEdit.selectedClips.erase(
                    std::remove_if(doc.sessionEdit.selectedClips.begin(), doc.sessionEdit.selectedClips.end(),
                                   [&](uint64_t cid) { return std::find(copies.begin(), copies.end(), cid) != copies.end(); }),
                    doc.sessionEdit.selectedClips.end());
            } else {
                for (auto& c : tr->clips)
                    if (std::find(chain.begin(), chain.end(), c.numId) != chain.end()) {
                        c.loopId.reset();
                        c.group.reset();
                    }
            }
            copies += static_cast<int>(chain.size() - 1);
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

    if (std::strcmp(cmd, "track.input") == 0) {
        const std::string inName = p.str("input").value_or("none");
        for (uint64_t tid : trackNums(*this, p)) {
            if (Session::Track* t = Session::findTrackByNum(doc, tid)) {
                if (inName == "none") {
                    t->mixer.input = Session::Route::none();
                } else if (const Session::Bus* b = Session::findBusByName(doc, inName)) {
                    t->mixer.input = Session::Route::bus(b->numId);
                } else {
                    t->mixer.input = Session::Route::hardware(inName);
                }
            }
        }
        Result result; result.ok = true; return result;
    }

    if (std::strcmp(cmd, "track.output") == 0) {
        const std::string outName = p.str("output").value_or("main");
        for (uint64_t tid : trackNums(*this, p)) {
            if (Session::Track* t = Session::findTrackByNum(doc, tid)) {
                if (outName == "none") {
                    t->mixer.output = Session::Route::none();
                } else if (outName == "main") {
                    t->mixer.output = Session::Route::mainOut();
                } else if (const Session::Bus* b = Session::findBusByName(doc, outName)) {
                    t->mixer.output = Session::Route::bus(b->numId);
                } else {
                    t->mixer.output = Session::Route::hardware(outName);
                }
            }
        }
        Result result; result.ok = true; return result;
    }

    if (std::strcmp(cmd, "mix.record_arm") == 0) {
        const bool val = p.boolean("value").value_or(true);
        for (uint64_t tid : trackNums(*this, p)) {
            if (Session::Track* t = Session::findTrackByNum(doc, tid)) {
                t->recordArm = val;
            }
        }
        Result result; result.ok = true; return result;
    }

    if (std::strcmp(cmd, "file.bounce_mix") == 0) {
        const auto pathOpt = p.str("path");
        if (!pathOpt || pathOpt->empty()) return bad("`path` required");
        Time::Range r = rangeParam(*this, p);
        if (r.isEmpty()) r = {0, std::max<long long>(1, Session::contentEnd(doc))};
        const std::filesystem::path fpath = std::filesystem::u8path(*pathOpt);
        const std::string ext = fpath.extension().u8string();
        const std::string fmtName = p.str("format").value_or(ext);
        const auto fmtOpt = RecordBounce::encodeFormatFor(fmtName);
        if (!fmtOpt.has_value()) {
            return bad("cannot write `" + fmtName + "` files (supported: wav, aiff, flac, mp3)");
        }
        RecordBounce::EncodeOptions opts;
        opts.format = *fmtOpt;
        const std::string bitStr = p.str("bit_depth").value_or("24");
        if (bitStr == "16") opts.bitDepth = RecordBounce::BitDepth::Int16;
        else if (bitStr == "32" || bitStr == "32f") opts.bitDepth = RecordBounce::BitDepth::Float32;
        else opts.bitDepth = RecordBounce::BitDepth::Int24;
        opts.dither = p.boolean("dither").value_or(true);

        std::optional<uint64_t> busId;
        if (auto srcName = p.str("source")) {
            if (!srcName->empty() && *srcName != "main" && *srcName != "Main") {
                const Session::Bus* b = Session::findBusByName(doc, *srcName);
                if (!b) {
                    std::string wanted = *srcName;
                    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    for (const auto& candidate : doc.busses) {
                        std::string name = candidate.name;
                        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (name == wanted) { b = &candidate; break; }
                    }
                }
                if (!b) return bad("unknown bus `" + *srcName + "`");
                busId = b->numId;
            }
        }
        const bool normalize = p.boolean("normalize").value_or(false);
        auto [job, err] = RecordBounce::Bounce::create(doc, fpath, r, opts, busId, normalize, false);
        if (!job) return bad(err);
        auto [done, runErr] = job->run();
        if (!done.has_value()) return bad(runErr);
        Result res; res.ok = true;
        res.nums["bytes"] = static_cast<double>(done->bytes);
        res.nums["peak_db"] = static_cast<double>(done->peakDb);
        res.nums["lufs"] = static_cast<double>(done->lufs);
        res.nums["true_peak_db"] = static_cast<double>(done->truePeakDb);
        return res;
    }

    if (std::strcmp(cmd, "file.bounce_stems") == 0) {
        const auto dirOpt = p.str("dir");
        if (!dirOpt || dirOpt->empty()) return bad("`dir` required");
        if (p.has("format") && !p.str("format").has_value()) {
            return bad("`format` must be a string");
        }
        const std::string fmtName = p.str("format").value_or("wav");
        const auto fmtOpt = RecordBounce::encodeFormatFor(fmtName);
        if (!fmtOpt.has_value()) {
            return bad("cannot write `" + fmtName + "` files (supported: wav, aiff, flac, mp3)");
        }
        RecordBounce::EncodeOptions opts;
        opts.format = *fmtOpt;
        const std::string bitStr = p.str("bit_depth").value_or("24");
        if (bitStr == "16") opts.bitDepth = RecordBounce::BitDepth::Int16;
        else if (bitStr == "32" || bitStr == "32f") opts.bitDepth = RecordBounce::BitDepth::Float32;
        else opts.bitDepth = RecordBounce::BitDepth::Int24;
        opts.dither = p.boolean("dither").value_or(true);
        Time::Range r = rangeParam(*this, p);
        if (r.isEmpty()) r = {0, std::max<long long>(1, Session::contentEnd(doc))};
        auto [files, err] = RecordBounce::bounceStems(doc, *dirOpt, r, opts, false);
        if (!err.empty()) return bad(err);
        return Result::num("files", static_cast<double>(files.size()));
    }

    if (std::strcmp(cmd, "file.export_clips") == 0) {
        const auto dirOpt = p.str("dir");
        if (!dirOpt || dirOpt->empty()) return bad("`dir` required");
        const auto ids = clipIds(*this, p);
        if (ids.empty()) return bad("no clips selected");
        auto [written, err] = RecordBounce::exportClips(doc, ids, *dirOpt);
        if (!err.empty()) return bad(err);
        return Result::num("written", static_cast<double>(written));
    }

    return Result::err(std::string("unknown command ") + cmd);
}

} // namespace EngCmd
