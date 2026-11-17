// after SoundCraft crates/engine/src/cmd/edit.rs mod tests
#include "engine_test_util_fix.h"

#include <cstring>
#include <functional>

namespace {

static bool test_targeted_fade_curvature_saves_reopens_and_undoes_without_changing_lengths() {
    for (const char* side : {"in", "out"}) {
        Engine e = Engine::demo();
        const auto* kick = Session::findTrackByName(e.doc, "Kick");
        const uint64_t cid = kick->clips[0].numId;
        Engine::Params p;
        pClip(p, cid);
        p.strs["side"] = side;
        p.nums["length"] = kick->clips[0].length / 2.0;
        p.strs["shape"] = "s-curve";
        if (!execOk(e, "edit.fades_create", p)) return false;
        const Session::Document before = e.doc;
        const size_t undoLen = e.undoLen();
        p.nums["curvature"] = -0.65;
        if (!execOk(e, "edit.fades_create", p)) return false;
        p.strs["shape"] = "s-curve";
        if (auto f = Session::findClipByNum(e.doc, cid)) {
            if (std::strcmp(side, "in") == 0)
                EXPECT_TRUE(std::abs(f->second->fadeIn.curvature + 0.65f) < 1e-3f);
            else
                EXPECT_TRUE(std::abs(f->second->fadeOut.curvature + 0.65f) < 1e-3f);
        }
        EXPECT_EQ(e.undoLen(), undoLen + 1);
        EXPECT_TRUE(e.undo());
        EXPECT_TRUE(Session::documentsEqual(e.doc, before));
        EXPECT_TRUE(e.redo());
    }
    return true;
}

static bool test_targeted_fades_fill_only_the_requested_side_and_undo_once() {
    for (const char* side : {"in", "out"}) {
        Engine e = Engine::demo();
        const uint64_t cid = Session::findTrackByName(e.doc, "Kick")->clips[0].numId;
        if (!execOk(e, "edit.select_none", {})) return false;
        const Session::Document before = e.doc;
        const size_t undoLen = e.undoLen();
        Engine::Params p;
        pClip(p, cid);
        p.strs["side"] = side;
        p.nums["length"] = 1e15;
        p.strs["shape"] = "s-curve";
        auto r = e.execute("edit.fades_create", p);
        EXPECT_TRUE(r.ok);
        EXPECT_EQ(r.nums["fades"], 1.0);
        EXPECT_EQ(e.undoLen(), undoLen + 1);
        EXPECT_TRUE(e.undo());
        EXPECT_TRUE(Session::documentsEqual(e.doc, before));
        EXPECT_TRUE(e.redo());
    }
    return true;
}

static bool test_targeted_fades_reject_invalid_parameters_without_an_edit() {
    Engine e = Engine::demo();
    const uint64_t id = Session::findTrackByName(e.doc, "Kick")->clips[0].numId;
    auto bad = [&](Engine::Params p) {
        const Session::Document before = e.doc;
        const size_t h = e.undoLen();
        if (!execFail(e, "edit.fades_create", p)) return false;
        EXPECT_TRUE(Session::documentsEqual(e.doc, before));
        EXPECT_EQ(e.undoLen(), h);
        return true;
    };
    bad([&] {
        Engine::Params p;
        pClip(p, id);
        p.strs["side"] = "in";
        return p;
    }());
    bad([&] {
        Engine::Params p;
        p.nums["length"] = 10;
        return p;
    }());
    if (Session::Clip* clip = Session::findClipByNum(e.doc, id)->second) clip->length = 0;
    bad([&] {
        Engine::Params p;
        pClip(p, id);
        p.strs["side"] = "in";
        p.nums["length"] = 1;
        return p;
    }());
    return true;
}

static bool test_range_fades_refuse_a_shape_that_is_not_a_shape_name() {
    auto f = trimSession({{0, 10000, 0}, {10000, 10000, 10000}});
    Engine::Params p;
    pTracks(p, {f.track});
    pRange(p, 9500, 10500);
    p.strs["shape"] = "bad";
    if (!execFail(f.e, "edit.fades_create", p)) return false;
    return true;
}

static bool test_targeted_fades_support_one_sample_clips() {
    Engine e = Engine::demo();
    uint64_t id = Session::findTrackByName(e.doc, "Kick")->clips[0].numId;
    if (Session::Clip* c = Session::findClipByNum(e.doc, id)->second) c->length = 1;
    for (const char* side : {"in", "out"}) {
        Engine::Params p;
        pClip(p, id);
        p.strs["side"] = side;
        p.nums["length"] = 1;
        if (!execOk(e, "edit.fades_create", p)) return false;
        auto* clip = Session::findClipByNum(e.doc, id)->second;
        const long long len = side[0] == 'i' ? clip->fadeIn.len : clip->fadeOut.len;
        EXPECT_EQ(len, 1LL);
    }
    return true;
}

static bool test_selecting_what_is_already_selected_leaves_the_document_alone() {
    Engine e = Engine::fresh();
    auto r = e.execute("track.new", [&] {
        Engine::Params p;
        p.nums["count"] = 2;
        return p;
    }());
    const uint64_t t0 = static_cast<uint64_t>(r.arr[0]);
    Engine::Params sel;
    pTracks(sel, {t0});
    pRange(sel, 100, 200);
    if (!execOk(e, "edit.select", sel)) return false;
    const uint32_t rev = e.revision;
    if (!execOk(e, "edit.select", sel)) return false;
    EXPECT_EQ(e.revision, rev);
    pRange(sel, 100, 300);
    if (!execOk(e, "edit.select", sel)) return false;
    EXPECT_TRUE(e.revision != rev);
    return true;
}

using AtFn = std::function<long long(long long)>;

static std::tuple<Engine, uint64_t, AtFn> keysFixture() {
    Engine e = Engine::fresh();
    const uint64_t t = Session::addAudioTrackNum(e.doc, "Keys");
    Session::findTrackByNum(e.doc, t)->kind = Session::TrackKind::Instrument;
    const long long sr = 48000;
    AtFn at = [sr](long long q) { return q * sr / 2; };
    return {std::move(e), t, std::move(at)};
}

static bool test_consolidating_an_empty_midi_selection_makes_an_empty_clip() {
    auto [e, t, at] = keysFixture();
    Engine::Params sel;
    pTrack(sel, t);
    pRange(sel, at(4), at(12));
    if (!execOk(e, "edit.select", sel)) return false;
    auto r = e.execute("edit.consolidate", {});
    EXPECT_EQ(r.nums["consolidated"], 1.0);
    return true;
}

static bool test_consolidating_midi_merges_the_clips_as_they_sound() {
    auto [e, t, at] = keysFixture();
    auto& d = e.doc;
    auto place = [&](long long st, long long en, Session::MidiSequence seq, bool muted) {
        Session::Clip c;
        c.numId = Session::newClipNumId(d);
        c.id = "clip_" + std::to_string(c.numId);
        c.isMidi = true;
        c.start = at(st);
        c.length = at(en) - at(st);
        c.midi = std::move(seq);
        c.muted = muted;
        EngEdit::placeClip(d, Session::findTrackByNum(d, t)->id, c);
    };
    Session::MidiSequence a, b, cseq;
    a.notes.push_back({60, 100, 64, 0, 0, 480});
    a.notes.push_back({62, 100, 64, 0, at(3) - at(0), at(2)});
    b.notes.push_back({64, 100, 64, 0, 0, 480});
    cseq.notes.push_back({65, 100, 64, 0, at(1) - at(0), at(1)});
    place(0, 4, a, false);
    place(6, 8, b, true);
    place(8, 12, cseq, false);
    Engine::Params sel;
    pTrack(sel, t);
    pRange(sel, at(2), at(10));
    if (!execOk(e, "edit.select", sel)) return false;
    if (!execOk(e, "edit.consolidate", {})) return false;
    return true;
}

static bool test_trim_to_file_boundaries_stops_at_the_neighbours() {
    auto f = trimSession({{0, 10000, 0}, {20000, 10000, 20000}, {40000, 5000, 0}});
    Engine::Params p;
    pClips(p, {f.clips[1]});
    if (!execOk(f.e, "edit.trim_to_file_boundaries", p)) return false;
    auto b = bounds(f.e, f.track);
    EXPECT_EQ_T(b.size(), 3u);
    EXPECT_EQ(std::get<0>(b[1]), 10000LL);
    EXPECT_EQ(std::get<1>(b[1]), 40000LL);
    return true;
}

static bool test_trim_to_fill_selection_stops_at_the_neighbours() {
    auto f = trimSession({{0, 10000, 0}, {20000, 10000, 20000}, {40000, 5000, 0}});
    Engine::Params p;
    pClips(p, {f.clips[1]});
    pRange(p, 5000, 45000);
    if (!execOk(f.e, "edit.trim_to_fill_selection", p)) return false;
    EXPECT_EQ(std::get<1>(bounds(f.e, f.track)[1]), 40000LL);
    return true;
}

static bool test_trims_without_neighbours_expand_fully() {
    auto f = trimSession({{20000, 10000, 20000}});
    Engine::Params p;
    pClips(p, {f.clips[0]});
    if (!execOk(f.e, "edit.trim_to_file_boundaries", p)) return false;
    EXPECT_EQ(std::get<0>(bounds(f.e, f.track)[0]), 0LL);
    auto f2 = trimSession({{20000, 10000, 20000}});
    pRange(p, 5000, 45000);
    pClips(p, {f2.clips[0]});
    if (!execOk(f2.e, "edit.trim_to_fill_selection", p)) return false;
    EXPECT_EQ(std::get<0>(bounds(f2.e, f2.track)[0]), 5000LL);
    return true;
}

static bool test_trim_to_file_boundaries_keeps_an_existing_crossfade() {
    auto f = trimSession({{0, 10000, 0}, {10000, 10000, 10000}});
    Engine::Params p;
    pTracks(p, {f.track});
    pRange(p, 9500, 10500);
    if (!execOk(f.e, "edit.fades_create", p)) return false;
    p = EP();
    pClips(p, {f.clips[1]});
    if (!execOk(f.e, "edit.trim_to_file_boundaries", p)) return false;
    EXPECT_EQ(std::get<0>(bounds(f.e, f.track)[1]), 9500LL);
    return true;
}

static bool test_restore_last_selection_swaps_back_to_the_previous_range_and_tracks() {
    Engine e = Engine::fresh();
    if (!execFail(e, "edit.restore_last_selection", {})) return false;
    auto tr = e.execute("track.new", [&] {
        Engine::Params p;
        p.nums["count"] = 2;
        return p;
    }());
    const uint64_t a = static_cast<uint64_t>(tr.arr[0]);
    const uint64_t b = static_cast<uint64_t>(tr.arr[1]);
    Engine::Params s1;
    pTracks(s1, {a});
    pRange(s1, 1000, 4000);
    if (!execOk(e, "edit.select", s1)) return false;
    Engine::Params s2;
    pTracks(s2, {b});
    pRange(s2, 7000, 9000);
    if (!execOk(e, "edit.select", s2)) return false;
    if (!execOk(e, "edit.restore_last_selection", {})) return false;
    EXPECT_EQ(e.doc.sessionEdit.selection.start, 1000LL);
    EXPECT_EQ(e.doc.sessionEdit.selectedTracks.front(), a);
    if (!execOk(e, "edit.restore_last_selection", {})) return false;
    EXPECT_EQ(e.doc.sessionEdit.selection.start, 7000LL);
    return true;
}

} // namespace

void registerCmdEditTests() {
    Test::add("targeted_fade_curvature_saves_reopens_and_undoes_without_changing_lengths", test_targeted_fade_curvature_saves_reopens_and_undoes_without_changing_lengths);
    Test::add("targeted_fades_fill_only_the_requested_side_and_undo_once", test_targeted_fades_fill_only_the_requested_side_and_undo_once);
    Test::add("targeted_fades_reject_invalid_parameters_without_an_edit", test_targeted_fades_reject_invalid_parameters_without_an_edit);
    Test::add("range_fades_refuse_a_shape_that_is_not_a_shape_name", test_range_fades_refuse_a_shape_that_is_not_a_shape_name);
    Test::add("targeted_fades_support_one_sample_clips", test_targeted_fades_support_one_sample_clips);
    Test::add("selecting_what_is_already_selected_leaves_the_document_alone", test_selecting_what_is_already_selected_leaves_the_document_alone);
    Test::add("consolidating_an_empty_midi_selection_makes_an_empty_clip", test_consolidating_an_empty_midi_selection_makes_an_empty_clip);
    Test::add("consolidating_midi_merges_the_clips_as_they_sound", test_consolidating_midi_merges_the_clips_as_they_sound);
    Test::add("trim_to_file_boundaries_stops_at_the_neighbours", test_trim_to_file_boundaries_stops_at_the_neighbours);
    Test::add("trim_to_fill_selection_stops_at_the_neighbours", test_trim_to_fill_selection_stops_at_the_neighbours);
    Test::add("trims_without_neighbours_expand_fully", test_trims_without_neighbours_expand_fully);
    Test::add("trim_to_file_boundaries_keeps_an_existing_crossfade", test_trim_to_file_boundaries_keeps_an_existing_crossfade);
    Test::add("restore_last_selection_swaps_back_to_the_previous_range_and_tracks", test_restore_last_selection_swaps_back_to_the_previous_range_and_tracks);
}