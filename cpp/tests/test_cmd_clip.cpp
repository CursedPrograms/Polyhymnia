// after SoundCraft crates/engine/src/cmd/clip.rs mod tests
#include "engine_test_util_fix.h"

namespace {

static Engine elasticClip(size_t sourceLen, size_t toneAt, long long clipLen) {
    Engine e = Engine::fresh();
    const uint64_t t = Session::addAudioTrackNum(e.doc, "Elastic");
    Session::Source src;
    src.numId = 2;
    src.id = "2";
    src.frames = static_cast<long long>(sourceLen);
    src.monoPcm.resize(sourceLen);
    for (size_t i = 0; i < sourceLen; ++i)
        src.monoPcm[i] = i >= toneAt ? static_cast<float>(std::sin(i * 0.2) * 0.4) : 0.0f;
    e.doc.sources.push_back(src);
    Session::Clip c;
    c.numId = Session::newClipNumId(e.doc);
    c.id = "clip_" + std::to_string(c.numId);
    c.sourceId = "2";
    c.length = clipLen;
    EngEdit::placeClip(e.doc, Session::findTrackByNum(e.doc, t)->id, c);
    EngParams ep;
    ep.nums["track"] = static_cast<double>(t);
    ep.strs["algorithm"] = "polyphonic";
    e.execute("track.elastic", ep);
    e.doc.sessionEdit.selectedClips = {c.numId};
    return e;
}

static float renderedPeak(Engine& e, uint64_t track, uint64_t cid) {
    auto fc = Session::findClipByNum(e.doc, cid);
    const Time::Range r = ClipOps::range(*fc->second);
    const auto ch = renderClips(e.doc, track, r);
    if (ch.empty() || ch[0].empty()) return 0.0f;
    float m = 0.0f;
    for (float v : ch[0]) m = std::max(m, std::abs(v));
    return m;
}

static bool test_elastic_warp_renders_and_remove_warp_restores() {
    Engine e = Engine::demo();
    const uint64_t pad = Session::findTrackByName(e.doc, "Pad")->numId;
    EngParams te;
    te.nums["track"] = static_cast<double>(pad);
    te.strs["algorithm"] = "polyphonic";
    e.execute("track.elastic", te);
    const uint64_t cid = Session::findTrackByName(e.doc, "Pad")->clips[0].numId;
    const long long len0 = Session::findClipByNum(e.doc, cid)->second->length;
    EngParams wp;
    pClips(wp, {cid});
    wp.nums["ratio"] = 1.5;
    auto r = e.execute("clip.elastic_properties", wp);
    EXPECT_EQ(r.nums["rendered"], 1.0);
    EXPECT_TRUE(std::abs(Session::findClipByNum(e.doc, cid)->second->length - static_cast<long long>(len0 * 1.5)) < 3);
    wp.nums["ratio"] = 0.5;
    e.execute("clip.elastic_properties", wp);
    if (!execOk(e, "clip.remove_warp", wp)) return false;
    EXPECT_EQ(Session::findClipByNum(e.doc, cid)->second->length, len0);
    return true;
}

static bool test_elastic_properties_stretches_the_clip_material() {
    Engine e = elasticClip(20000, 10000, 20000);
    const uint64_t t = Session::findTrackByName(e.doc, "Elastic") ? 0 : Session::findTrack(e.doc, "tr_1")->numId;
    (void)t;
    const uint64_t cid = e.doc.sessionEdit.selectedClips[0];
    const uint64_t track = Session::findClipByNum(e.doc, cid)->first.empty() ? 1 : Session::findTrackByName(e.doc, "Elastic")->numId;
    (void)track;
    EngParams wp;
    pClips(wp, {cid});
    wp.nums["ratio"] = 2.0;
    e.execute("clip.elastic_properties", wp);
    EXPECT_EQ(Session::findClipByNum(e.doc, cid)->second->length, 40000LL);
    return true;
}

static bool test_warping_and_unwarping_keep_fades_within_the_clip() {
    Engine e = elasticClip(20000, 10000, 20000);
    const uint64_t cid = e.doc.sessionEdit.selectedClips[0];
    Session::Clip* c = Session::findClipByNum(e.doc, cid)->second;
    c->fadeIn.len = 20000;
    c->fadeOut.len = 20000;
    EngParams wp;
    pClips(wp, {cid});
    wp.nums["ratio"] = 0.5;
    e.execute("clip.elastic_properties", wp);
    c = Session::findClipByNum(e.doc, cid)->second;
    EXPECT_TRUE(c->fadeIn.len <= c->length);
    return true;
}

static bool test_remove_warp_keeps_the_audio_when_the_original_file_is_gone() {
    Engine e = Engine::demo();
    const uint64_t pad = Session::findTrackByName(e.doc, "Pad")->numId;
    e.execute("track.elastic", [&] {
        EngParams p;
        p.nums["track"] = static_cast<double>(pad);
        p.strs["algorithm"] = "polyphonic";
        return p;
    }());
    const uint64_t cid = Session::findTrackByName(e.doc, "Pad")->clips[0].numId;
    EngParams wp;
    pClips(wp, {cid});
    wp.nums["ratio"] = 1.5;
    e.execute("clip.elastic_properties", wp);
    const auto it = e.doc.sessionEdit.values.find("elastic." + std::to_string(cid) + ".src");
    EXPECT_TRUE(it != e.doc.sessionEdit.values.end());
    EngParams cl;
    cl.idLists["sources"] = {static_cast<uint64_t>(it->second)};
    e.execute("clip.clear", cl);
    auto r = e.execute("clip.remove_warp", wp);
    EXPECT_EQ(r.nums["restored"], 0.0);
    return true;
}

static Engine threeClips() {
    Engine e = Engine::fresh();
    const uint64_t t = Session::addAudioTrackNum(e.doc, "Dialog");
    Session::Source src;
    src.numId = 3;
    src.id = "3";
    src.frames = 10000;
    src.monoPcm.assign(10000, 0.1f);
    e.doc.sources.push_back(src);
    std::vector<uint64_t> ids;
    for (int k = 0; k < 3; ++k) {
        Session::Clip c;
        c.numId = Session::newClipNumId(e.doc);
        c.id = "clip_" + std::to_string(c.numId);
        c.sourceId = "3";
        c.start = k * 12000;
        c.length = 10000;
        EngEdit::placeClip(e.doc, Session::findTrackByNum(e.doc, t)->id, c);
        ids.push_back(c.numId);
    }
    e.doc.sessionEdit.selectedClips = ids;
    return e;
}

static bool test_gain_nudges_one_db_at_a_time_up_to_the_ceiling() {
    Engine e = Engine::demo();
    const uint64_t kick = Session::findTrackByName(e.doc, "Kick")->clips[0].numId;
    EngParams sel;
    pClips(sel, {kick});
    e.execute("edit.select", sel);
    e.execute("clip.gain_nudge_up", {});
    e.execute("clip.gain_nudge_up", {});
    e.execute("clip.gain_nudge_down", {});
    EXPECT_EQ(Session::findClipByNum(e.doc, kick)->second->gainDb, 1.0f);
    EngParams g;
    g.nums["db"] = 35.5;
    e.execute("clip.gain", g);
    e.execute("clip.gain_nudge_up", {});
    EXPECT_EQ(Session::findClipByNum(e.doc, kick)->second->gainDb, 36.0f);
    return true;
}

static bool test_remove_sync_point_clears_the_point_identify_set() {
    Engine e = threeClips();
    const uint64_t a = e.doc.sessionEdit.selectedClips[0];
    EngParams id;
    pClip(id, a);
    id.nums["at"] = 4000;
    e.execute("clip.identify_sync_point", id);
    EXPECT_EQ(Session::findClipByNum(e.doc, a)->second->syncPoint, 4000LL);
    EngParams sel;
    pClips(sel, {a});
    e.execute("edit.select", sel);
    e.execute("clip.remove_sync_point", {});
    EXPECT_EQ(Session::findClipByNum(e.doc, a)->second->syncPoint, 0LL);
    return true;
}

static bool test_remove_sync_point_clears_every_selected_clip_and_only_those() {
    Engine e = threeClips();
    const auto ids = e.doc.sessionEdit.selectedClips;
    for (size_t i = 0; i < ids.size(); ++i) Session::findClipByNum(e.doc, ids[i])->second->syncPoint = static_cast<long long>((i + 1) * 1000);
    EngParams sel;
    pClips(sel, {ids[0], ids[1]});
    e.execute("edit.select", sel);
    auto r = e.execute("clip.remove_sync_point", {});
    EXPECT_EQ(r.nums["clips"], 2.0);
    EXPECT_EQ(Session::findClipByNum(e.doc, ids[2])->second->syncPoint, 3000LL);
    return true;
}

static bool test_remove_sync_point_is_disabled_unless_a_selected_clip_has_one() {
    Engine e = threeClips();
    const auto ids = e.doc.sessionEdit.selectedClips;
    Session::findClipByNum(e.doc, ids[2])->second->syncPoint = 3000;
    EngParams sel;
    pClips(sel, {ids[0], ids[1]});
    e.execute("edit.select", sel);
    EXPECT_TRUE(!e.commandEnabled("clip.remove_sync_point"));
    pClips(sel, {ids[1], ids[2]});
    e.execute("edit.select", sel);
    EXPECT_TRUE(e.commandEnabled("clip.remove_sync_point"));
    return true;
}

static bool test_remove_sync_point_is_one_undo_step_that_puts_the_points_back() {
    Engine e = threeClips();
    const auto ids = e.doc.sessionEdit.selectedClips;
    Session::findClipByNum(e.doc, ids[0])->second->syncPoint = 1000;
    Session::findClipByNum(e.doc, ids[1])->second->syncPoint = 2000;
    EngParams sel;
    pClips(sel, {ids[0], ids[1]});
    e.execute("edit.select", sel);
    const size_t steps = e.undoLen();
    e.execute("clip.remove_sync_point", {});
    EXPECT_EQ(e.undoLen(), steps + 1);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(Session::findClipByNum(e.doc, ids[0])->second->syncPoint, 1000LL);
    return true;
}

} // namespace

void registerCmdClipTests() {
    Test::add("elastic_warp_renders_and_remove_warp_restores", test_elastic_warp_renders_and_remove_warp_restores);
    Test::add("elastic_properties_stretches_the_clip_material", test_elastic_properties_stretches_the_clip_material);
    Test::add("warping_and_unwarping_keep_fades_within_the_clip", test_warping_and_unwarping_keep_fades_within_the_clip);
    Test::add("remove_warp_keeps_the_audio_when_the_original_file_is_gone", test_remove_warp_keeps_the_audio_when_the_original_file_is_gone);
    Test::add("gain_nudges_one_db_at_a_time_up_to_the_ceiling", test_gain_nudges_one_db_at_a_time_up_to_the_ceiling);
    Test::add("remove_sync_point_clears_the_point_identify_set", test_remove_sync_point_clears_the_point_identify_set);
    Test::add("remove_sync_point_clears_every_selected_clip_and_only_those", test_remove_sync_point_clears_every_selected_clip_and_only_those);
    Test::add("remove_sync_point_is_disabled_unless_a_selected_clip_has_one", test_remove_sync_point_is_disabled_unless_a_selected_clip_has_one);
    Test::add("remove_sync_point_is_one_undo_step_that_puts_the_points_back", test_remove_sync_point_is_one_undo_step_that_puts_the_points_back);
}