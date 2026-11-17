// after SoundCraft crates/engine/src/cmd/clip_more.rs mod tests
#include "engine_test_util_fix.h"
#include "clip_ops.h"

namespace {

static Engine engineWith(std::initializer_list<std::tuple<long long, long long, long long>> clips) {
    Engine e = Engine::fresh();
    const uint64_t t = Session::addAudioTrackNum(e.doc, "c");
    Session::Source src;
    src.numId = 9;
    src.id = "9";
    src.frames = 200000;
    e.doc.sources.push_back(src);
    e.doc.sessionEdit.selectedTracks = {t};
    for (const auto& item : clips) {
        const long long st = std::get<0>(item);
        const long long len = std::get<1>(item);
        const long long off = std::get<2>(item);
        Session::Clip c;
        c.numId = Session::newClipNumId(e.doc);
        c.id = "clip_" + std::to_string(c.numId);
        c.sourceId = "9";
        c.start = st;
        c.length = len;
        c.offset = off;
        EngEdit::placeClip(e.doc, Session::findTrackByNum(e.doc, t)->id, c);
    }
    return e;
}

static Engine midiEngine() {
    Engine e = Engine::fresh();
    const uint64_t t = Session::addAudioTrackNum(e.doc, "m");
    Session::findTrackByNum(e.doc, t)->kind = Session::TrackKind::Midi;
    Session::Clip c;
    c.numId = Session::newClipNumId(e.doc);
    c.id = "clip_" + std::to_string(c.numId);
    c.isMidi = true;
    c.length = 100;
    c.midi = Session::MidiSequence{};
    c.midi->notes.push_back({60, 100, 64, 0, 0, 240});
    EngEdit::placeClip(e.doc, Session::findTrackByNum(e.doc, t)->id, c);
    e.doc.sessionEdit.selectedTracks = {t};
    e.doc.sessionEdit.selectedClips = {c.numId};
    return e;
}

static std::vector<std::pair<long long, long long>> spans(Engine& e, uint64_t t) {
    std::vector<std::pair<long long, long long>> out;
    for (const auto& c : Session::findTrackByNum(e.doc, t)->clips) out.emplace_back(c.start, c.length);
    return out;
}

static bool test_effects_set_caps_gain() {
    Engine e = engineWith({{0, 100, 0}});
    const uint64_t id = Session::findTrackByNum(e.doc, e.doc.sessionEdit.selectedTracks[0])->clips[0].numId;
    EngParams p;
    pClip(p, id);
    p.objects["params"] = {{"gain", 500.0}};
    e.execute("clip.effects_set", p);
    const double gain = e.doc.sessionEdit.values["clip_fx." + std::to_string(id) + ".gain"];
    EXPECT_EQ(gain, 24.0);
    return true;
}

static bool test_unloop_removes_copies() {
    // after SoundCraft cmd/clip_more.rs unloop_removes_copies: four touching copies, no clip.loop first
    Engine e = engineWith({{0, 100, 0}, {100, 100, 0}, {200, 100, 0}, {300, 100, 50}});
    const uint64_t t = e.doc.sessionEdit.selectedTracks[0];
    const auto& clips = Session::findTrackByNum(e.doc, t)->clips;
    const uint64_t id0 = clips[0].numId;
    const uint64_t id1 = clips[1].numId;
    const uint64_t id3 = clips[3].numId;
    EngParams ul;
    pClip(ul, id1);
    if (!execOk(e, "clip.unloop", ul)) return false;
    std::vector<uint64_t> left;
    for (const auto& c : Session::findTrackByNum(e.doc, t)->clips) left.push_back(c.numId);
    EXPECT_EQ(left.size(), 2u);
    EXPECT_EQ(left[0], id0);
    EXPECT_EQ(left[1], id3);
    return true;
}

static bool test_unloop_recognises_loops_after_source_edit() {
    Engine e = midiEngine();
    const uint64_t id = e.doc.sessionEdit.selectedClips[0];
    EngParams lp;
    pClip(lp, id);
    lp.nums["count"] = 4;
    const auto lr0 = e.execute("clip.loop", lp);
    EXPECT_EQ(resultNum(lr0, "loops"), 3.0);
    Session::findClipByNum(e.doc, id)->second->midi->notes[0].length = 1100;
    if (!execOk(e, "clip.unloop", lp)) return false;
    const uint64_t tr = e.doc.sessionEdit.selectedTracks[0];
    EXPECT_EQ(Session::findTrackByNum(e.doc, tr)->clips.size(), 1u);
    return true;
}

static bool test_unloop_flatten_turns_loop_into_independent_clips() {
    Engine e = midiEngine();
    const uint64_t id = e.doc.sessionEdit.selectedClips[0];
    EngParams lp;
    pClip(lp, id);
    lp.nums["count"] = 3;
    e.execute("clip.loop", lp);
    EngParams ul;
    pClip(ul, id);
    ul.strs["mode"] = "flatten";
    e.execute("clip.unloop", ul);
    const uint64_t tr = e.doc.sessionEdit.selectedTracks[0];
    EXPECT_EQ(Session::findTrackByNum(e.doc, tr)->clips.size(), 3u);
    return true;
}

static bool test_loop_length_cuts_the_last_iteration_short() {
    Engine e = midiEngine();
    const uint64_t id = e.doc.sessionEdit.selectedClips[0];
    const uint64_t t = e.doc.sessionEdit.selectedTracks[0];
    EngParams lp;
    pClip(lp, id);
    lp.nums["length"] = 250;
    const auto lr = e.execute("clip.loop", lp);
    EXPECT_EQ(resultNum(lr, "loops"), 2.0);
    const auto sp = spans(e, t);
    EXPECT_TRUE(sp.size() == 3 && sp[0].first == 0 && sp[2].second == 50);
    return true;
}

static bool test_a_short_last_iteration_fits_its_fades_and_drops_a_sync_point_past_the_cut() {
    Engine e = midiEngine();
    const uint64_t id = e.doc.sessionEdit.selectedClips[0];
    const uint64_t t = e.doc.sessionEdit.selectedTracks[0];
    Session::Clip* c = Session::findClipByNum(e.doc, id)->second;
    c->syncPoint = 80;
    c->fadeOut.len = 70;
    EngParams lp;
    pClip(lp, id);
    lp.nums["length"] = 150;
    e.execute("clip.loop", lp);
    const auto& clips = Session::findTrackByNum(e.doc, t)->clips;
    const auto& last = clips.back();
    EXPECT_EQ(last.start, 100LL);
    EXPECT_EQ(last.length, 50LL);
    EXPECT_EQ(last.syncPoint, 0LL);
    return true;
}

static bool test_unloop_removes_a_short_last_iteration_too() {
    Engine e = midiEngine();
    const uint64_t id = e.doc.sessionEdit.selectedClips[0];
    const uint64_t t = e.doc.sessionEdit.selectedTracks[0];
    EngParams lp;
    pClip(lp, id);
    lp.nums["length"] = 250;
    e.execute("clip.loop", lp);
    EngParams ul;
    pClip(ul, id);
    if (!execOk(e, "clip.unloop", ul)) return false;
    EXPECT_EQ(spans(e, t).size(), 1u);
    return true;
}

static bool test_unloop_keeps_both_halves_of_a_split_clip() {
    Engine e = engineWith({{0, 100, 0}});
    const uint64_t t = e.doc.sessionEdit.selectedTracks[0];
    const uint64_t id = Session::findTrackByNum(e.doc, t)->clips[0].numId;
    EngParams lp;
    pClip(lp, id);
    lp.nums["count"] = 3;
    e.execute("clip.loop", lp);
    EngEdit::separateAt(e.doc, Session::findTrackByNum(e.doc, t)->id, 230);
    if (!execOk(e, "clip.unloop", lp)) return false;
    return true;
}

static bool test_loop_fill_stops_at_the_next_clip() {
    Engine e = engineWith({{0, 100, 0}, {430, 100, 0}});
    const uint64_t t = e.doc.sessionEdit.selectedTracks[0];
    const uint64_t id = Session::findTrackByNum(e.doc, t)->clips[0].numId;
    EngParams lp;
    pClip(lp, id);
    lp.bools["fill"] = true;
    const auto lr = e.execute("clip.loop", lp);
    EXPECT_EQ(resultNum(lr, "loops"), 4.0);
    const size_t n6 = Session::findTrackByNum(e.doc, t)->clips.size();
    EXPECT_EQ(n6, 6u);
    return true;
}

static bool test_loop_fill_without_a_next_clip_runs_to_the_session_end() {
    Engine e = engineWith({{0, 100, 0}});
    const uint64_t other = Session::addAudioTrackNum(e.doc, "far");
    Session::Clip farc;
    farc.numId = Session::newClipNumId(e.doc);
    farc.id = "clip_" + std::to_string(farc.numId);
    farc.sourceId = "9";
    farc.start = 200;
    farc.length = 150;
    EngEdit::placeClip(e.doc, Session::findTrackByNum(e.doc, other)->id, farc);
    const uint64_t t = e.doc.sessionEdit.selectedTracks[0];
    const uint64_t id = Session::findTrackByNum(e.doc, t)->clips[0].numId;
    EngParams lp;
    pClip(lp, id);
    lp.bools["fill"] = true;
    const auto lr3 = e.execute("clip.loop", lp);
    EXPECT_EQ(resultNum(lr3, "loops"), 3.0);
    return true;
}

static bool test_a_fill_or_length_past_the_loop_limit_is_refused_without_an_edit() {
    Engine e = engineWith({{0, 100, 0}, {100150, 100, 0}});
    const uint64_t t = e.doc.sessionEdit.selectedTracks[0];
    const uint64_t id = Session::findTrackByNum(e.doc, t)->clips[0].numId;
    EngParams lp;
    pClip(lp, id);
    lp.bools["fill"] = true;
    if (!execFail(e, "clip.loop", lp)) return false;
    lp.bools.clear();
    lp.nums["length"] = 100200;
    if (!execFail(e, "clip.loop", lp)) return false;
    const size_t nClips = Session::findTrackByNum(e.doc, t)->clips.size();
    EXPECT_EQ(nClips, 2u);
    return true;
}

static bool test_regroup_reuses_group() {
    Engine e = engineWith({{0, 100, 0}, {500, 100, 0}, {900, 100, 0}});
    const uint64_t t = e.doc.sessionEdit.selectedTracks[0];
    std::vector<uint64_t> ids;
    for (const auto& c : Session::findTrackByNum(e.doc, t)->clips) ids.push_back(c.numId);
    EngParams gp;
    pClips(gp, ids);
    e.execute("clip.group", gp);
    const uint64_t g = Session::findClipByNum(e.doc, ids[0])->second->group.value();
    gp = EP();
    pClips(gp, {ids[2]});
    e.execute("clip.ungroup", gp);
    e.execute("clip.regroup", [&] {
        EngParams p;
        pClips(p, ids);
        return p;
    }());
    for (uint64_t id : ids) EXPECT_EQ(Session::findClipByNum(e.doc, id)->second->group.value(), g);
    return true;
}

static bool test_effects_bypass_and_clear() {
    Engine e = engineWith({{0, 100, 0}});
    const uint64_t id = Session::findTrackByNum(e.doc, e.doc.sessionEdit.selectedTracks[0])->clips[0].numId;
    EngParams p;
    pClip(p, id);
    p.objects["params"] = {{"eq_gain_db", 3.0}};
    e.execute("clip.effects_set", p);
    e.execute("clip.effects_bypass", p);
    EXPECT_TRUE(e.doc.sessionEdit.flags["clip_fx.bypass." + std::to_string(id)]);
    e.execute("edit.copy_clip_effects", p);
    e.execute("edit.clear_clip_effects", p);
    EXPECT_TRUE(e.doc.sessionEdit.values.empty());
    e.execute("edit.paste_clip_effects", p);
    EXPECT_EQ(e.doc.sessionEdit.values.size(), 1u);
    return true;
}

static bool test_conform_follows_tempo() {
    Engine e = engineWith({{0, 48000, 0}});
    const uint64_t id = Session::findTrackByNum(e.doc, e.doc.sessionEdit.selectedTracks[0])->clips[0].numId;
    EngParams p;
    pClip(p, id);
    e.execute("clip.conform_to_tempo", p);
    e.execute("event.tempo", [&] {
        EngParams tp;
        tp.nums["bpm"] = 60;
        return tp;
    }());
    e.execute("clip.conform_to_tempo", p);
    EXPECT_EQ(Session::findClipByNum(e.doc, id)->second->length, 96000LL);
    return true;
}

static bool test_remove_pitch_shift_renders() {
    Engine e = Engine::fresh();
    const uint64_t t = Session::addAudioTrackNum(e.doc, "tone");
    Session::Source src;
    src.numId = 4;
    src.id = "4";
    src.frames = 12000;
    src.monoPcm.resize(12000);
    for (size_t i = 0; i < src.monoPcm.size(); ++i) src.monoPcm[i] = static_cast<float>(std::sin(i * 0.07) * 0.5);
    e.doc.sources.push_back(src);
    Session::Clip c;
    c.numId = Session::newClipNumId(e.doc);
    c.id = "clip_" + std::to_string(c.numId);
    c.sourceId = "4";
    c.length = 18000;
    c.stretch = 1.5;
    EngEdit::placeClip(e.doc, Session::findTrackByNum(e.doc, t)->id, c);
    e.doc.sessionEdit.selectedClips = {c.numId};
    e.doc.sessionEdit.selectedTracks = {t};
    const EngResult rr = e.execute("clip.remove_pitch_shift", {});
    EXPECT_EQ(rr.nums.at("clips"), 1.0);
    EXPECT_EQ(Session::findClipByNum(e.doc, c.numId)->second->stretch, 1.0);
    return true;
}

static bool test_render_preserves_stretched_audio() {
    Engine e = Engine::fresh();
    const uint64_t t = Session::addAudioTrackNum(e.doc, "tone");
    Session::Source src;
    src.numId = 5;
    src.id = "5";
    src.frames = 9600;
    src.monoPcm.resize(9600);
    for (size_t i = 0; i < src.monoPcm.size(); ++i) src.monoPcm[i] = static_cast<float>(std::sin(i * 0.05) * 0.4);
    e.doc.sources.push_back(src);
    Session::Clip c;
    c.numId = Session::newClipNumId(e.doc);
    c.id = "clip_" + std::to_string(c.numId);
    c.sourceId = "5";
    c.length = 9600;
    EngEdit::placeClip(e.doc, Session::findTrackByNum(e.doc, t)->id, c);
    EngParams gp;
    pClip(gp, c.numId);
    gp.nums["db"] = -6;
    e.execute("clip.gain", gp);
    if (!execOk(e, "clip.gain_render", gp)) return false;
    EXPECT_EQ(Session::findClipByNum(e.doc, c.numId)->second->gainDb, 0.0f);
    return true;
}

} // namespace

void registerCmdClipMoreTests() {
    Test::add("effects_set_caps_gain", test_effects_set_caps_gain);
    Test::add("unloop_removes_copies", test_unloop_removes_copies);
    Test::add("unloop_recognises_loops_after_source_edit", test_unloop_recognises_loops_after_source_edit);
    Test::add("unloop_flatten_turns_loop_into_independent_clips", test_unloop_flatten_turns_loop_into_independent_clips);
    Test::add("loop_length_cuts_the_last_iteration_short", test_loop_length_cuts_the_last_iteration_short);
    Test::add("a_short_last_iteration_fits_its_fades_and_drops_a_sync_point_past_the_cut", test_a_short_last_iteration_fits_its_fades_and_drops_a_sync_point_past_the_cut);
    Test::add("unloop_removes_a_short_last_iteration_too", test_unloop_removes_a_short_last_iteration_too);
    Test::add("unloop_keeps_both_halves_of_a_split_clip", test_unloop_keeps_both_halves_of_a_split_clip);
    Test::add("loop_fill_stops_at_the_next_clip", test_loop_fill_stops_at_the_next_clip);
    Test::add("loop_fill_without_a_next_clip_runs_to_the_session_end", test_loop_fill_without_a_next_clip_runs_to_the_session_end);
    Test::add("a_fill_or_length_past_the_loop_limit_is_refused_without_an_edit", test_a_fill_or_length_past_the_loop_limit_is_refused_without_an_edit);
    Test::add("regroup_reuses_group", test_regroup_reuses_group);
    Test::add("effects_bypass_and_clear", test_effects_bypass_and_clear);
    Test::add("conform_follows_tempo", test_conform_follows_tempo);
    Test::add("remove_pitch_shift_renders", test_remove_pitch_shift_renders);
    Test::add("render_preserves_stretched_audio", test_render_preserves_stretched_audio);
}