// after SoundCraft crates/engine/src/cmd/edit_more.rs mod tests
#include "engine_test_util_fix.h"
#include "clip_ops.h"

namespace {

struct ClipSess {
    Engine e;
    uint64_t track = 0;
    std::vector<uint64_t> clips;
};

static ClipSess sessionWithClips(std::initializer_list<std::pair<long long, long long>> starts) {
    ClipSess out;
    out.e = Engine::fresh();
    out.track = Session::addAudioTrackNum(out.e.doc, "c");
    Session::Source src;
    src.numId = 1;
    src.id = "1";
    src.frames = 100000;
    out.e.doc.sources.push_back(src);
    auto& d = out.e.doc;
    d.sessionEdit.selectedTracks = {out.track};
    for (auto [st, len] : starts) {
        Session::Clip c;
        c.numId = Session::newClipNumId(d);
        c.id = "clip_" + std::to_string(c.numId);
        c.sourceId = "1";
        c.start = st;
        c.length = len;
        EngEdit::placeClip(d, Session::findTrackByNum(d, out.track)->id, c);
        out.clips.push_back(c.numId);
    }
    return out;
}

static std::vector<long long> starts(Engine& e, uint64_t t) {
    std::vector<long long> s;
    for (const auto& c : Session::findTrackByNum(e.doc, t)->clips) s.push_back(c.start);
    return s;
}

static bool test_merge_markers_pastes_copied_markers_once() {
    Engine e = Engine::demo();
    const auto& first = e.doc.markers.front();
    EngParams sel;
    pTracks(sel, {Session::findTrackByName(e.doc, "Kick")->numId});
    pRange(sel, first.start - 10, first.start + 10);
    if (!execOk(e, "edit.select", sel)) return false;
    if (!execOk(e, "edit.copy", {})) return false;
    const size_t before = e.doc.markers.size();
    EngParams paste;
    paste.nums["at"] = 5000000;
    auto r = e.execute("edit.paste_merge_markers", paste);
    EXPECT_EQ(r.nums["added"], 1.0);
    EXPECT_EQ(e.doc.markers.size(), before + 1);
    const EngResult r2 = e.execute("edit.paste_merge_markers", paste);
    EXPECT_EQ(resultNum(r2, "added"), 0.0);
    return true;
}

static bool test_space_clips_butts_with_gap() {
    auto s = sessionWithClips({{0, 100}, {500, 100}, {2000, 50}});
    EngParams p;
    pClips(p, s.clips);
    p.nums["gap"] = 10;
    if (!execOk(s.e, "edit.space_clips", p)) return false;
    const auto st = starts(s.e, s.track);
    EXPECT_TRUE(st.size() == 3 && st[0] == 0 && st[1] == 110 && st[2] == 220);
    EXPECT_TRUE(s.e.canUndo());
    return true;
}

static bool test_snap_next_and_previous() {
    auto s = sessionWithClips({{0, 100}, {300, 100}, {1000, 100}});
    EngParams p;
    pClip(p, s.clips[1]);
    if (!execOk(s.e, "edit.snap_previous", p)) return false;
    EXPECT_EQ(starts(s.e, s.track)[1], 100LL);
    if (!execOk(s.e, "edit.snap_next", p)) return false;
    EXPECT_EQ(starts(s.e, s.track)[1], 900LL);
    return true;
}

static bool test_cut_all_automation_can_be_pasted() {
    auto s = sessionWithClips({{0, 1000}});
    EngParams ap;
    ap.nums["track"] = static_cast<double>(s.track);
    ap.strs["param"] = "volume";
    ap.nums["at"] = 100;
    ap.nums["value"] = -6;
    s.e.execute("automation.set_point", ap);
    ap.nums["at"] = 400;
    ap.nums["value"] = -12;
    s.e.execute("automation.set_point", ap);
    EngParams cut;
    pRange(cut, 50, 250);
    auto r = s.e.execute("edit.cut_all_automation", cut);
    EXPECT_TRUE(r.nums["lanes"] >= 1.0);
    EngParams paste;
    paste.nums["at"] = 1000;
    if (!execOk(s.e, "edit.paste_to_current_automation", paste)) return false;
    return true;
}

static bool test_snap_selected_neighbours_keeps_both_clips() {
    auto s = sessionWithClips({{0, 100}, {300, 100}, {1000, 100}});
    EngParams p;
    pClips(p, {s.clips[0], s.clips[1]});
    if (!execOk(s.e, "edit.snap_next", p)) return false;
    EXPECT_EQ(Session::findTrackByNum(s.e.doc, s.track)->clips.size(), 3u);
    const auto st = starts(s.e, s.track);
    EXPECT_TRUE(st.size() == 3 && st[0] == 800 && st[1] == 900 && st[2] == 1000);
    auto s2 = sessionWithClips({{0, 100}, {300, 100}, {1000, 100}});
    pClips(p, {s2.clips[0], s2.clips[1]});
    if (!execOk(s2.e, "edit.snap_previous", p)) return false;
    const auto st2 = starts(s2.e, s2.track);
    EXPECT_TRUE(st2.size() == 3 && st2[0] == 0 && st2[1] == 100 && st2[2] == 1000);
    return true;
}

static bool test_copy_and_paste_automation_only() {
    auto s = sessionWithClips({{0, 1000}});
    EngParams ap;
    ap.nums["track"] = static_cast<double>(s.track);
    ap.strs["param"] = "pan";
    ap.nums["at"] = 100;
    ap.nums["value"] = -1;
    s.e.execute("automation.set_point", ap);
    ap.nums["at"] = 200;
    ap.nums["value"] = 1;
    s.e.execute("automation.set_point", ap);
    EngParams cut;
    pRange(cut, 50, 250);
    s.e.execute("edit.cut_pan_automation", cut);
    EXPECT_EQ(s.e.clipboard.automation[0].size(), 1u);
    EngParams view;
    view.strs["view"] = "volume";
    s.e.execute("track.view", view);
    EngParams paste;
    paste.nums["at"] = 1000;
    if (!execOk(s.e, "edit.paste_to_current_automation", paste)) return false;
    return true;
}

static bool test_clip_gain_copy_paste() {
    auto s = sessionWithClips({{0, 1000}, {2000, 1000}});
    EngParams g;
    pClip(g, s.clips[0]);
    g.nums["db"] = -6;
    s.e.execute("clip.gain", g);
    EngParams cp;
    pRange(cp, 0, 1000);
    s.e.execute("edit.copy_clip_gain", cp);
    EngParams pp;
    pp.nums["at"] = 2000;
    s.e.execute("edit.paste_clip_gain", pp);
    const auto& c = Session::findTrackByNum(s.e.doc, s.track)->clips[1];
    EXPECT_TRUE(std::abs(c.gainDb + 6.0f) < 0.1f);
    return true;
}

static bool test_cut_clip_gain_can_be_pasted() {
    auto s = sessionWithClips({{0, 1000}, {2000, 1000}, {4000, 1000}});
    EngParams g;
    pClip(g, s.clips[0]);
    g.nums["db"] = -6;
    s.e.execute("clip.gain", g);
    pClip(g, s.clips[2]);
    g.nums["db"] = -12;
    s.e.execute("clip.gain", g);
    EngParams cp;
    pRange(cp, 4000, 5000);
    s.e.execute("edit.copy_clip_gain", cp);
    EngParams sel;
    pTrack(sel, s.track);
    pClips(sel, {s.clips[0]});
    pRange(sel, 0, 1000);
    s.e.execute("edit.select", sel);
    if (!execOk(s.e, "edit.cut_clip_gain", {})) return false;
    EngParams pp;
    pp.nums["at"] = 2000;
    s.e.execute("edit.paste_clip_gain", pp);
    EXPECT_TRUE(std::abs(Session::findTrackByNum(s.e.doc, s.track)->clips[1].gainDb + 6.0f) < 0.1f);
    return true;
}

static bool test_move_to_new_playlist_clears_main() {
    auto s = sessionWithClips({{0, 1000}});
    EngParams mv;
    pRange(mv, 200, 400);
    if (!execOk(s.e, "edit.move_to_new_playlist", mv)) return false;
    const Session::Track* tr = Session::findTrackByNum(s.e.doc, s.track);
    EXPECT_EQ(tr->playlists.size(), 1u);
    EXPECT_EQ(tr->playlists[0].clips.size(), 1u);
    const Session::Clip* moved = &tr->playlists[0].clips[0];
    EXPECT_TRUE(moved->start >= 200LL && moved->length <= 200LL);
    EngParams cp;
    pRange(cp, 0, 100);
    if (!execFail(s.e, "edit.copy_to_target_playlist", cp)) return false;
    s.e.execute("track.playlist_select", [&] {
        EngParams p;
        p.nums["index"] = 1;
        return p;
    }());
    if (!execOk(s.e, "track.designate_target_playlist", {})) return false;
    s.e.execute("track.playlist_select", [&] {
        EngParams p;
        p.nums["index"] = 0;
        return p;
    }());
    if (!execOk(s.e, "edit.copy_to_target_playlist", cp)) return false;
    EXPECT_EQ(Session::findTrackByNum(s.e.doc, s.track)->playlists[0].clips.size(), 2u);
    return true;
}

static bool test_coalesce_volume_into_clip_gain_and_back() {
    auto s = sessionWithClips({{0, 1000}});
    EngParams ap;
    ap.nums["track"] = static_cast<double>(s.track);
    ap.nums["at"] = 0;
    ap.nums["value"] = -10;
    s.e.execute("automation.set_point", ap);
    ap.nums["at"] = 1000;
    ap.nums["value"] = 0;
    s.e.execute("automation.set_point", ap);
    if (!execOk(s.e, "edit.automation_coalesce_volume_to_clip_gain", {})) return false;
    EXPECT_TRUE(std::abs(Session::findTrackByNum(s.e.doc, s.track)->clips[0].gainDb + 5.0f) < 0.1f);
    if (!execOk(s.e, "edit.automation_coalesce_clip_gain_to_volume", {})) return false;
    return true;
}

static bool test_glide_and_trim() {
    auto s = sessionWithClips({{0, 1000}});
    EngParams ap;
    ap.nums["track"] = static_cast<double>(s.track);
    ap.nums["at"] = 0;
    ap.nums["value"] = -10;
    s.e.execute("automation.set_point", ap);
    s.e.execute("mix.volume", [&] {
        EngParams p;
        p.nums["db"] = 0;
        return p;
    }());
    EngParams glide;
    pRange(glide, 100, 300);
    if (!execOk(s.e, "edit.automation_glide_to_all", glide)) return false;
    EngParams trim;
    pRange(trim, 400, 500);
    if (!execOk(s.e, "edit.automation_trim_to_all", trim)) return false;
    return true;
}

static bool test_merge_midi_keeps_existing_notes() {
    Engine e = Engine::demo();
    const uint64_t keys = Session::findTrackByName(e.doc, "Keys")->numId;
    const size_t before = Session::findTrackByName(e.doc, "Keys")->clips[0].midi->notes.size();
    EngParams sel;
    pTrack(sel, keys);
    pRange(sel, Session::findTrackByName(e.doc, "Keys")->clips[0].start, Session::findTrackByName(e.doc, "Keys")->clips[0].start + 48000);
    e.execute("edit.select", sel);
    e.execute("edit.copy", {});
    const size_t copied = e.clipboard.tracks.empty() ? 0 : e.clipboard.tracks[0].size();
    (void)copied;
    EngParams pm;
    pm.nums["at"] = Session::findTrackByName(e.doc, "Keys")->clips[0].start + 96000;
    e.execute("edit.paste_merge_midi", pm);
    const size_t after = Session::findTrackByName(e.doc, "Keys")->clips[0].midi->notes.size();
    EXPECT_TRUE(after >= before);
    return true;
}

static bool test_tce_fits_timeline() {
    Engine e = Engine::fresh();
    const uint64_t t = Session::addAudioTrackNum(e.doc, "tone");
    Session::Source src;
    src.numId = 1;
    src.id = "1";
    src.frames = 24000;
    src.monoPcm.resize(24000);
    for (size_t i = 0; i < src.monoPcm.size(); ++i) src.monoPcm[i] = static_cast<float>(std::sin(i * 0.05) * 0.5);
    e.doc.sources.push_back(src);
    Session::Clip c;
    c.numId = Session::newClipNumId(e.doc);
    c.id = "clip_" + std::to_string(c.numId);
    c.sourceId = "1";
    c.length = 24000;
    EngEdit::placeClip(e.doc, Session::findTrackByNum(e.doc, t)->id, c);
    e.doc.sessionEdit.selectedClips = {c.numId};
    e.doc.sessionEdit.timelineSelection = {1000, 31000};
    auto r = e.execute("edit.tce_to_timeline", {});
    EXPECT_TRUE(std::abs(r.nums["ratio"] - 1.25) < 1e-3);
    return true;
}

static bool test_extend_to_members() {
    Engine e = Engine::fresh();
    e.execute("track.new", [&] {
        EngParams p;
        p.nums["count"] = 2;
        return p;
    }());
    e.execute("track.move_to_new_folder", {});
    const uint64_t folder = e.doc.tracks[0].numId;
    EngParams sel;
    pTracks(sel, {folder});
    e.execute("edit.select", sel);
    e.execute("edit.extend_down_to_members", {});
    EXPECT_EQ(e.doc.sessionEdit.selectedTracks.size(), 3u);
    return true;
}

static bool test_marker_lane_selection() {
    Engine e = Engine::fresh();
    e.execute("edit.marker_lane_move_down", {});
    e.execute("edit.marker_lane_extend_down", {});
    auto r = e.execute("edit.marker_lane_extend_up", {});
    EXPECT_EQ(r.arr.size(), 2u);
    EXPECT_EQ(r.arr[0], 1.0);
    EXPECT_EQ(r.arr[1], 3.0);
    r = e.execute("edit.marker_lane_move_up", {});
    EXPECT_EQ(r.arr[0], 1.0);
    EXPECT_EQ(r.arr[1], 1.0);
    return true;
}

static bool test_duplicate_and_extend() {
    auto s = sessionWithClips({{0, 100}});
    EngParams p;
    pRange(p, 0, 100);
    if (!execOk(s.e, "edit.duplicate_extend", p)) return false;
    const auto st = starts(s.e, s.track);
    EXPECT_TRUE(st.size() == 2 && st[0] == 0 && st[1] == 100);
    EXPECT_EQ(s.e.doc.sessionEdit.selection.end, 200LL);
    return true;
}

} // namespace

void registerCmdEditMoreTests() {
    Test::add("merge_markers_pastes_copied_markers_once", test_merge_markers_pastes_copied_markers_once);
    Test::add("space_clips_butts_with_gap", test_space_clips_butts_with_gap);
    Test::add("snap_next_and_previous", test_snap_next_and_previous);
    Test::add("cut_all_automation_can_be_pasted", test_cut_all_automation_can_be_pasted);
    Test::add("snap_selected_neighbours_keeps_both_clips", test_snap_selected_neighbours_keeps_both_clips);
    Test::add("copy_and_paste_automation_only", test_copy_and_paste_automation_only);
    Test::add("clip_gain_copy_paste", test_clip_gain_copy_paste);
    Test::add("cut_clip_gain_can_be_pasted", test_cut_clip_gain_can_be_pasted);
    Test::add("move_to_new_playlist_clears_main", test_move_to_new_playlist_clears_main);
    Test::add("coalesce_volume_into_clip_gain_and_back", test_coalesce_volume_into_clip_gain_and_back);
    Test::add("glide_and_trim", test_glide_and_trim);
    Test::add("merge_midi_keeps_existing_notes", test_merge_midi_keeps_existing_notes);
    Test::add("tce_fits_timeline", test_tce_fits_timeline);
    Test::add("extend_to_members", test_extend_to_members);
    Test::add("marker_lane_selection", test_marker_lane_selection);
    Test::add("duplicate_and_extend", test_duplicate_and_extend);
}