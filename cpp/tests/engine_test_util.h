#pragma once
#include "engine.h"
#include "engine_edit.h"
#include "test_runner.h"

#include <tuple>
#include <vector>

inline Engine::Params EP() { return {}; }

inline void pTrack(Engine::Params& p, uint64_t id) { p.idLists["tracks"] = {id}; p.idLists["track"] = {id}; }

inline void pTracks(Engine::Params& p, std::initializer_list<uint64_t> ids) { p.idLists["tracks"] = ids; }

inline void pClip(Engine::Params& p, uint64_t id) { p.idLists["clip"] = {id}; p.idLists["clips"] = {id}; }

inline void pClips(Engine::Params& p, std::initializer_list<uint64_t> ids) { p.idLists["clips"] = ids; }

inline void pRange(Engine::Params& p, long long s, long long e) {
    p.nums["start"] = static_cast<double>(s);
    p.nums["end"] = static_cast<double>(e);
}

inline bool execOk(Engine& e, const char* cmd, const Engine::Params& p = {}) {
    const auto r = e.execute(cmd, p);
    if (!r.ok) {
        std::fprintf(stderr, "execute %s: %s\n", cmd, r.error.c_str());
        return false;
    }
    return true;
}

inline bool execFail(Engine& e, const char* cmd, const Engine::Params& p = {}) { return !e.execute(cmd, p).ok; }

struct TrimFixture {
    Engine e;
    uint64_t track = 0;
    std::vector<uint64_t> clips;
};

inline TrimFixture trimSession(std::initializer_list<std::tuple<long long, long long, long long>> clips) {
    TrimFixture f;
    f.e = Engine::fresh();
    f.track = Session::addAudioTrackNum(f.e.doc, "c");
    auto& d = f.e.doc;
    Session::Source src;
    src.numId = 9;
    src.id = "9";
    src.path = "src";
    src.sampleRate = 48000;
    src.channels = 1;
    src.frames = 60000;
    src.monoPcm.assign(60000, 0.25f);
    d.sources.push_back(std::move(src));
    for (const auto& item : clips) {
        const long long st = std::get<0>(item);
        const long long len = std::get<1>(item);
        const long long off = std::get<2>(item);
        Session::Clip c;
        c.numId = Session::newClipNumId(d);
        c.id = "clip_" + std::to_string(c.numId);
        c.sourceId = "9";
        c.offset = off;
        c.start = st;
        c.length = len;
        EngEdit::placeClip(d, Session::findTrackByNum(d, f.track)->id, c);
        f.clips.push_back(c.numId);
    }
    return f;
}

inline std::vector<std::tuple<long long, long long, long long>> bounds(Engine& e, uint64_t tr) {
    std::vector<std::tuple<long long, long long, long long>> out;
    const Session::Track* t = Session::findTrackByNum(e.doc, tr);
    for (const auto& c : t->clips) out.emplace_back(c.start, c.end(), ClipOps::sourceOffset(c));
    return out;
}