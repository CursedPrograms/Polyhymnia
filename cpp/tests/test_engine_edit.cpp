// Ports of edit/tests.rs (after SoundCraft crates/engine/src/edit/tests.rs).
#include "clip_ops.h"
#include "engine_edit.h"
#include "session.h"
#include "test_runner.h"
#include "time_grid.h"

#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using ClipSpan = std::tuple<long long, long long, long long>;

Session::Clip makeAudio(Session::Document& s, const char* name, long long offset, long long start, long long len) {
    Session::Clip c;
    c.id = Session::newClipId(s);
    c.name = name;
    c.sourceId = "500";
    c.offset = offset;
    c.start = start;
    c.length = len;
    return c;
}

std::pair<Session::Document, std::string> sessionWithClip() {
    Session::Document s = Session::empty();
    const std::string t = Session::addAudioTrack(s);
    Session::Source src;
    src.id = "500";
    src.path = "src.wav";
    src.sampleRate = 48000;
    src.channels = 1;
    src.frames = 10000;
    s.sources.push_back(src);
    Session::Track* tr = Session::findTrack(s, t);
    tr->clips.push_back(makeAudio(s, "a", 1000, 1000, 4000));
    return {std::move(s), t};
}

std::vector<ClipSpan> clipSpans(const Session::Document& s, const std::string& t) {
    std::vector<ClipSpan> out;
    const Session::Track* tr = Session::findTrack(s, t);
    for (const auto& c : tr->clips) out.emplace_back(c.start, c.length, ClipOps::sourceOffset(c));
    return out;
}

std::string addMidiTrack(Session::Document& s) {
    Session::Track tr;
    tr.id = "midi_" + std::to_string(s.tracks.size() + 1);
    tr.name = "MIDI";
    tr.kind = Session::TrackKind::Midi;
    s.tracks.push_back(std::move(tr));
    return s.tracks.back().id;
}

struct ThreeClips {
    Session::Document s;
    std::string t;
    std::string a, b, c;
};

ThreeClips threeClips() {
    auto [s, t] = sessionWithClip();
    ThreeClips out;
    out.s = std::move(s);
    out.t = t;
    out.a = Session::newClipId(out.s);
    out.b = Session::newClipId(out.s);
    out.c = Session::newClipId(out.s);
    Session::Track* tr = Session::findTrack(out.s, t);
    tr->clips = {makeAudio(out.s, "a", 0, 0, 1000), makeAudio(out.s, "b", 1000, 1000, 2000), makeAudio(out.s, "c", 3000, 5000, 1000)};
    tr->clips[0].id = out.a;
    tr->clips[1].id = out.b;
    tr->clips[2].id = out.c;
    return out;
}

bool pasteOk(EngEdit::PasteOutcome o) {
    if (o.error) {
        std::fprintf(stderr, "paste error: %s\n", o.error);
        return false;
    }
    return true;
}

bool shuffleOk(const EngEdit::ShuffleOutcome& o, size_t want) {
    if (o.error) {
        std::fprintf(stderr, "shuffle error: %s\n", o.error);
        return false;
    }
    if (o.count != want) {
        std::fprintf(stderr, "shuffle count %zu != %zu\n", o.count, want);
        return false;
    }
    return true;
}

bool expectClipSpans(const std::vector<ClipSpan>& got, std::initializer_list<ClipSpan> want) {
    const std::vector<ClipSpan> w(want);
    if (got == w) return true;
    std::fprintf(stderr, "  clip spans mismatch\n");
    return false;
}

bool expectPairs(const std::vector<std::pair<long long, long long>>& got,
                 std::initializer_list<std::pair<long long, long long>> want) {
    const std::vector<std::pair<long long, long long>> w(want);
    if (got == w) return true;
    std::fprintf(stderr, "  pair vector mismatch\n");
    return false;
}

bool expectTrackPairs(const std::vector<std::pair<std::string, std::string>>& got,
                      std::initializer_list<std::pair<std::string, std::string>> want) {
    const std::vector<std::pair<std::string, std::string>> w(want);
    if (got == w) return true;
    std::fprintf(stderr, "  track pair mismatch\n");
    return false;
}

bool expectFadePaste(const std::vector<std::tuple<long long, long long, long long, long long>>& got,
                     std::initializer_list<std::tuple<long long, long long, long long, long long>> want) {
    const std::vector<std::tuple<long long, long long, long long, long long>> w(want);
    if (got == w) return true;
    std::fprintf(stderr, "  paste fade mismatch\n");
    return false;
}

} // namespace

static bool test_copy_range_resets_fades_at_cut_edges() {
    auto [s, t] = sessionWithClip();
    if (Session::Clip* c = Session::clipMut(s, t, Session::findTrack(s, t)->clips[0].id)) {
        c->fadeIn = {100, Session::FadeShape::Linear, 0.0f};
        c->fadeOut = {100, Session::FadeShape::Linear, 0.0f};
    }
    auto fades = [&](Time::Range r) {
        std::vector<std::pair<long long, long long>> v;
        for (const auto& c : EngEdit::copyRange(s, t, r)) v.emplace_back(c.fadeIn.len, c.fadeOut.len);
        return v;
    };
    EXPECT_TRUE(expectPairs(fades(Time::Range::newRange(2000, 3000)), {{0, 0}}));
    EXPECT_TRUE(expectPairs(fades(Time::Range::newRange(0, 3000)), {{100, 0}}));
    EXPECT_TRUE(expectPairs(fades(Time::Range::newRange(2000, 6000)), {{0, 100}}));
    EXPECT_TRUE(expectPairs(fades(Time::Range::newRange(0, 6000)), {{100, 100}}));
    return true;
}

static bool test_separate_splits_and_heal_rejoins() {
    auto [s, t] = sessionWithClip();
    EXPECT_EQ(EngEdit::separateAt(s, t, 3000), 1u);
    EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 2000, 1000}, {3000, 2000, 3000}}));
    EXPECT_EQ(EngEdit::separateAt(s, t, 1000), 0u);
    EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(0, 10000)), 1u);
    EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 4000, 1000}}));
    return true;
}

static bool test_heal_rejoins_cut_separation() {
    {
        auto [s, t] = sessionWithClip();
        EngEdit::clearRange(s, t, Time::Range::newRange(2000, 3000), false);
        EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(1500, 3500)), 1u);
        EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 4000, 1000}}));
    }
    {
        auto [s, t] = sessionWithClip();
        EngEdit::clearRange(s, t, Time::Range::newRange(2000, 3000), false);
        EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(2500, 3500)), 0u);
    }
    {
        auto [s, t] = sessionWithClip();
        EngEdit::clearRange(s, t, Time::Range::newRange(2000, 3000), true);
        EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(0, 10000)), 0u);
    }
    {
        auto [s, t] = sessionWithClip();
        EngEdit::clearRange(s, t, Time::Range::newRange(2000, 3000), false);
        Session::findTrack(s, t)->clips[1].start = 3500;
        EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(0, 10000)), 0u);
    }
    {
        auto [s, t] = sessionWithClip();
        EngEdit::separateAt(s, t, 3000);
        Session::findTrack(s, t)->clips[1].start = 2500;
        EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(0, 10000)), 0u);
        EXPECT_EQ(clipSpans(s, t).size(), 2u);
    }
    return true;
}

static bool test_heal_skips_clips_with_different_playback() {
    const std::function<void(Session::Clip&)> tweaks[] = {
        [](Session::Clip& c) { c.muted = true; },
        [](Session::Clip& c) { c.stretch = 1.5; },
    };
    for (const auto& tweak : tweaks) {
        auto [s, t] = sessionWithClip();
        EngEdit::separateAt(s, t, 3000);
        tweak(Session::findTrack(s, t)->clips[1]);
        EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(0, 10000)), 0u);
        EXPECT_EQ(Session::findTrack(s, t)->clips.size(), 2u);
    }
    return true;
}

static bool test_heal_keeps_each_side_gain_envelope() {
    const std::vector<std::pair<std::vector<std::pair<long long, float>>, std::vector<std::pair<long long, float>>>> cases = {
        {{{500, -3.0f}, {2500, -12.0f}}, {}},
        {{}, {{0, -6.0f}, {1000, 0.0f}}},
        {{{0, 0.0f}, {1999, -20.0f}}, {{-100, 4.0f}, {300, -9.0f}}},
        {{{500, -3.0f}, {2500, -12.0f}}, {{0, -9.75f}, {500, -12.0f}}},
    };
    for (const auto& row : cases) {
        auto [s, t] = sessionWithClip();
        EngEdit::separateAt(s, t, 3000);
        Session::Track* tr = Session::findTrack(s, t);
        tr->clips[0].gainEnv = row.first;
        tr->clips[1].gainEnv = row.second;
        const Session::Clip l = tr->clips[0];
        const Session::Clip r = tr->clips[1];
        EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(0, 10000)), 1u);
        const Session::Clip& healed = Session::findTrack(s, t)->clips[0];
        for (long long rel = 0; rel < 4000; ++rel) {
            const float want = rel < 2000 ? ClipOps::gainAt(l, rel) : ClipOps::gainAt(r, rel - 2000);
            if (std::abs(ClipOps::gainAt(healed, rel) - want) >= 1e-4f) {
                std::fprintf(stderr, "gain mismatch at %lld: %g != %g\n", rel, ClipOps::gainAt(healed, rel), want);
                return false;
            }
        }
    }
    return true;
}

static bool test_heal_keeps_gain_envelopes_across_a_gap() {
    auto [s, t] = sessionWithClip();
    EngEdit::clearRange(s, t, Time::Range::newRange(2000, 3000), false);
    Session::Track* tr = Session::findTrack(s, t);
    tr->clips[0].gainEnv = {{0, 0.0f}, {999, -6.0f}};
    tr->clips[1].gainEnv = {{0, -12.0f}, {1999, -3.0f}};
    const Session::Clip l = tr->clips[0];
    const Session::Clip r = tr->clips[1];
    EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(1500, 3500)), 1u);
    const Session::Clip& healed = Session::findTrack(s, t)->clips[0];
    EXPECT_EQ(healed.length, 4000LL);
    for (long long rel = 0; rel < 1000; ++rel) {
        const float want = ClipOps::gainAt(l, rel);
        if (std::abs(ClipOps::gainAt(healed, rel) - want) >= 1e-4f) return false;
    }
    for (long long rel = 2000; rel < 4000; ++rel) {
        const float want = ClipOps::gainAt(r, rel - 2000);
        if (std::abs(ClipOps::gainAt(healed, rel) - want) >= 1e-4f) return false;
    }
    return true;
}

static bool test_split_then_heal_rejoins_stretched_clip() {
    auto [s, t] = sessionWithClip();
    Session::findTrack(s, t)->clips[0].stretch = 2.0;
    EXPECT_EQ(EngEdit::separateAt(s, t, 3000), 1u);
    EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 2000, 1000}, {3000, 2000, 2000}}));
    EXPECT_EQ(EngEdit::heal(s, t, Time::Range::newRange(0, 10000)), 1u);
    EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 4000, 1000}}));
    return true;
}

static bool test_clear_slip_and_shuffle() {
    {
        auto [s, t] = sessionWithClip();
        const auto removed = EngEdit::clearRange(s, t, Time::Range::newRange(2000, 3000), false);
        EXPECT_EQ(removed.size(), 1u);
        EXPECT_EQ(removed[0].start, 0LL);
        EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 1000, 1000}, {3000, 2000, 3000}}));
    }
    {
        auto [s, t] = sessionWithClip();
        EngEdit::clearRange(s, t, Time::Range::newRange(2000, 3000), true);
        EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 1000, 1000}, {2000, 2000, 3000}}));
    }
    return true;
}

static bool test_remove_clips_closes_each_gap_in_shuffle_only() {
    auto split = [](bool shuffle) {
        auto [s, t] = sessionWithClip();
        EngEdit::separateAt(s, t, 2000);
        EngEdit::separateAt(s, t, 3000);
        EngEdit::separateAt(s, t, 4000);
        std::vector<std::string> ids;
        for (const auto& c : Session::findTrack(s, t)->clips) ids.push_back(c.id);
        EngEdit::removeClips(s, {ids[0], ids[2]}, shuffle);
        return clipSpans(s, t);
    };
    EXPECT_TRUE(expectClipSpans(split(false), {{2000, 1000, 2000}, {4000, 1000, 4000}}));
    EXPECT_TRUE(expectClipSpans(split(true), {{1000, 1000, 2000}, {2000, 1000, 4000}}));
    return true;
}

static bool test_remove_clips_in_shuffle_keeps_crossfade_partners_apart() {
    auto removeSecond = [](const std::vector<std::pair<long long, long long>>& spans) {
        auto [s, t] = sessionWithClip();
        std::vector<std::string> ids;
        for (size_t i = 0; i < spans.size(); ++i) ids.push_back(Session::newClipId(s));
        std::vector<Session::Clip> clips;
        for (size_t i = 0; i < spans.size(); ++i) {
            const auto [a, b] = spans[i];
            auto c = makeAudio(s, "c", a, a, b - a);
            c.id = ids[i];
            clips.push_back(c);
        }
        Session::findTrack(s, t)->clips = std::move(clips);
        EngEdit::removeClips(s, {ids[1]}, true);
        std::vector<std::pair<long long, long long>> out;
        for (const auto& c : Session::findTrack(s, t)->clips) out.emplace_back(c.start, ClipOps::end(c));
        return out;
    };
    EXPECT_TRUE(expectPairs(removeSecond({{0, 1050}, {950, 2000}, {2000, 3000}}), {{0, 1050}, {1050, 2050}}));
    EXPECT_TRUE(expectPairs(removeSecond({{0, 1000}, {1000, 2050}, {1950, 3000}, {4000, 5000}}),
                            {{0, 1000}, {1000, 2050}, {3050, 4050}}));
    return true;
}

static bool test_copy_paste_overwrites() {
    auto [s, t] = sessionWithClip();
    const auto copied = EngEdit::copyRange(s, t, Time::Range::newRange(1500, 2500));
    EXPECT_EQ(copied.size(), 1u);
    EXPECT_EQ(copied[0].start, 0LL);
    EXPECT_EQ(copied[0].length, 1000LL);
    EXPECT_EQ(ClipOps::sourceOffset(copied[0]), 1500LL);
    EXPECT_TRUE(pasteOk(EngEdit::paste(s, t, 4000, copied, 1000, false)));
    EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 3000, 1000}, {4000, 1000, 1500}}));
    return true;
}

static bool test_paste_shuffle_ripples() {
    auto [s, t] = sessionWithClip();
    const auto copied = EngEdit::copyRange(s, t, Time::Range::newRange(1000, 2000));
    EXPECT_TRUE(pasteOk(EngEdit::paste(s, t, 1000, copied, 1000, true)));
    EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 1000, 1000}, {2000, 4000, 1000}}));
    return true;
}

static bool test_insert_silence_and_trim() {
    {
        auto [s, t] = sessionWithClip();
        EngEdit::insertSilence(s, t, Time::Range::newRange(2000, 2500));
        EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1000, 1000, 1000}, {2500, 3000, 2000}}));
    }
    {
        auto [s, t] = sessionWithClip();
        EngEdit::trimTo(s, t, Time::Range::newRange(1500, 2000));
        EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1500, 500, 1500}}));
    }
    return true;
}

static bool test_fades_and_crossfades() {
    {
        auto [s, t] = sessionWithClip();
        EXPECT_EQ(EngEdit::createFades(s, t, Time::Range::newRange(500, 1500), Session::FadeShape::Linear), 1u);
        EXPECT_EQ(Session::findTrack(s, t)->clips[0].fadeIn.len, 500LL);
    }
    {
        auto [s, t] = sessionWithClip();
        EngEdit::separateAt(s, t, 3000);
        EXPECT_EQ(EngEdit::createFades(s, t, Time::Range::newRange(2900, 3100), Session::FadeShape::EqualPower), 1u);
        const auto& c = Session::findTrack(s, t)->clips;
        EXPECT_EQ(ClipOps::end(c[0]), 3100LL);
        EXPECT_EQ(c[1].start, 2900LL);
        EXPECT_EQ(c[0].fadeOut.len, 200LL);
        EXPECT_EQ(c[1].fadeIn.len, 200LL);
        EngEdit::deleteFades(s, t, Time::Range::newRange(2900, 3100));
        const auto& c2 = Session::findTrack(s, t)->clips;
        EXPECT_EQ(ClipOps::end(c2[0]), 3000LL);
        EXPECT_EQ(c2[1].start, 3000LL);
    }
    return true;
}

static bool test_shuffle_moves_reorder_clips_and_keep_the_silence() {
    ThreeClips tc = threeClips();
    EXPECT_TRUE(shuffleOk(EngEdit::shuffleClips(tc.s, {tc.b}, -900, {}), 1));
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, tc.t), {{0, 2000, 1000}, {2000, 1000, 0}, {5000, 1000, 3000}}));
    tc = threeClips();
    EXPECT_EQ(EngEdit::shuffleSlot(tc.s, tc.t, 900, {tc.a}), 0LL);
    EXPECT_EQ(EngEdit::shuffleSlot(tc.s, tc.t, 1100, {tc.a}), 2000LL);
    EXPECT_TRUE(shuffleOk(EngEdit::shuffleClips(tc.s, {tc.a}, 2500, {}), 1));
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, tc.t), {{0, 2000, 1000}, {2000, 1000, 0}, {5000, 1000, 3000}}));
    return true;
}

static bool test_shuffling_onto_an_empty_track_starts_it_at_zero() {
    ThreeClips tc = threeClips();
    const std::string other = Session::addAudioTrack(tc.s);
    EXPECT_TRUE(shuffleOk(EngEdit::shuffleClips(tc.s, {tc.c}, 40000, {{tc.t, other}}), 1));
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, other), {{0, 1000, 3000}}));
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, tc.t), {{0, 1000, 0}, {1000, 2000, 1000}}));
    return true;
}

static bool test_shuffle_trims_move_later_clips_and_slip_trims_stop_at_neighbours() {
    ThreeClips tc = threeClips();
    EngEdit::trimClip(tc.s, tc.b, std::nullopt, 2500, true);
    EngEdit::trimClip(tc.s, tc.b, 1500, std::nullopt, true);
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, tc.t), {{0, 1000, 0}, {1000, 1000, 1500}, {4000, 1000, 3000}}));
    tc = threeClips();
    EngEdit::trimClip(tc.s, tc.a, std::nullopt, 2500, false);
    EngEdit::trimClip(tc.s, tc.c, 2000, std::nullopt, false);
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, tc.t), {{0, 1000, 0}, {1000, 2000, 1000}, {3000, 3000, 1000}}));
    EngEdit::trimClip(tc.s, tc.c, std::nullopt, 99999, false);
    EXPECT_EQ(clipSpans(tc.s, tc.t)[2], ClipSpan(3000, 9000, 1000));
    return true;
}

static bool test_shuffle_leaves_locked_clips_and_what_follows_them_in_place() {
    ThreeClips tc = threeClips();
    Session::clipMut(tc.s, tc.c)->timeLocked = true;
    EngEdit::clearRange(tc.s, tc.t, Time::Range::newRange(1000, 3000), true);
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, tc.t), {{0, 1000, 0}, {5000, 1000, 3000}}));
    tc = threeClips();
    Session::clipMut(tc.s, tc.c)->editLocked = true;
    EXPECT_EQ(EngEdit::shuffleRoom(tc.s, tc.t, 0), 2000LL);
    EXPECT_TRUE(EngEdit::makeRoom(tc.s, tc.t, 0, 2500) == EngEdit::NO_ROOM);
    EXPECT_TRUE(EngEdit::makeRoom(tc.s, tc.t, 0, 2000) == nullptr);
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, tc.t), {{2000, 1000, 0}, {3000, 2000, 1000}, {5000, 1000, 3000}}));
    EXPECT_TRUE(shuffleOk(EngEdit::shuffleClips(tc.s, {tc.a}, 2500, {}), 1));
    EXPECT_EQ(clipSpans(tc.s, tc.t)[2], ClipSpan(5000, 1000, 3000));
    tc = threeClips();
    Session::clipMut(tc.s, tc.c)->timeLocked = true;
    EngEdit::trimClip(tc.s, tc.b, std::nullopt, 9000, true);
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, tc.t), {{0, 1000, 0}, {1000, 4000, 1000}, {5000, 1000, 3000}}));
    EXPECT_EQ(EngEdit::shuffleSlot(tc.s, tc.t, 4000, {tc.a}), 4000LL);
    return true;
}

static bool test_multi_track_moves_keep_the_tracks_apart() {
    ThreeClips tc = threeClips();
    const std::string t2 = Session::addAudioTrack(tc.s);
    const std::string t3 = Session::addAudioTrack(tc.s);
    const std::string m = addMidiTrack(tc.s);
    const std::string d = Session::newClipId(tc.s);
    Session::findTrack(tc.s, t2)->clips.push_back(makeAudio(tc.s, "d", 0, 0, 500));
    Session::findTrack(tc.s, t2)->clips.back().id = d;
    const auto dest = EngEdit::destinations(tc.s, {tc.a, d}, tc.t, t2);
    EXPECT_TRUE(dest.has_value());
    EXPECT_TRUE(expectTrackPairs(dest.value(), {{tc.t, t2}, {t2, t3}}));
    EXPECT_TRUE(!EngEdit::destinations(tc.s, {tc.a, d}, tc.t, m).has_value());
    EXPECT_TRUE(!EngEdit::destinations(tc.s, {tc.a}, tc.t, m).has_value());
    EXPECT_EQ(EngEdit::moveClips(tc.s, {tc.a, d}, 0, dest.value()), 2u);
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, t2), {{0, 1000, 0}}));
    EXPECT_TRUE(expectClipSpans(clipSpans(tc.s, t3), {{0, 500, 0}}));
    return true;
}

static bool test_grid_moves_snap_the_clip_start_or_keep_its_offset() {
    auto [s, t] = sessionWithClip();
    const long long step =
        s.edit.grid.stepSamples(0, TimeGrid::sampleRateFromSession(s), TimeGrid::tempoMapFromSession(s), s.frameRate);
    const Session::Clip c = Session::findTrack(s, t)->clips[0];
    EXPECT_EQ(c.start + EngEdit::gridDelta(s, c, step + 200, false, {c.id}), step);
    EXPECT_EQ(c.start + EngEdit::gridDelta(s, c, step + 200, true, {c.id}), step + 1000);
    EXPECT_EQ(EngEdit::gridDelta(s, c, step / 3, true, {c.id}), 0LL);
    return true;
}

static bool test_moves_overwrite_and_respect_locks() {
    auto [s, t] = sessionWithClip();
    const std::string id = Session::findTrack(s, t)->clips[0].id;
    EXPECT_EQ(EngEdit::moveClips(s, {id}, 500, {}), 1u);
    EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{1500, 4000, 1000}}));
    Session::clipMut(s, id)->timeLocked = true;
    EXPECT_EQ(EngEdit::moveClips(s, {id}, 500, {}), 0u);
    return true;
}

static bool test_delete_fades_keeps_playlist_sorted_when_a_clip_overlaps_several() {
    auto [s, t] = sessionWithClip();
    Session::Track* tr = Session::findTrack(s, t);
    tr->clips.clear();
    tr->clips.push_back(makeAudio(s, "a", 0, 0, 1000));
    tr->clips.push_back(makeAudio(s, "b", 100, 100, 100));
    tr->clips.push_back(makeAudio(s, "c", 150, 150, 750));
    tr->clips[0].id = "901";
    tr->clips[1].id = "902";
    tr->clips[2].id = "903";
    EngEdit::deleteFades(s, t, Time::Range::newRange(0, 10000));
    const auto got = clipSpans(s, t);
    for (size_t i = 1; i < got.size(); ++i)
        if (std::get<0>(got[i - 1]) > std::get<0>(got[i])) {
            std::fprintf(stderr, "playlist not sorted\n");
            return false;
        }
    for (const auto& c : got)
        if (std::get<1>(c) <= 0) return false;
    return true;
}

static bool test_delete_fades_leaves_unrelated_overlaps_alone() {
    auto [s, t] = sessionWithClip();
    std::vector<std::string> ids;
    for (int i = 0; i < 4; ++i) ids.push_back(Session::newClipId(s));
    Session::Track* tr = Session::findTrack(s, t);
    tr->clips.clear();
    const long long layout[][2] = {{0, 1100}, {1000, 1000}, {5000, 1100}, {6000, 1000}};
    for (int i = 0; i < 4; ++i) {
        auto c = makeAudio(s, "c", layout[i][0], layout[i][0], layout[i][1]);
        c.id = ids[i];
        c.fadeIn = {100, Session::FadeShape::Linear, 0.0f};
        c.fadeOut = {100, Session::FadeShape::Linear, 0.0f};
        tr->clips.push_back(c);
    }
    EXPECT_EQ(EngEdit::deleteFades(s, t, Time::Range::newRange(900, 1200)), 2u);
    EXPECT_TRUE(expectClipSpans(clipSpans(s, t), {{0, 1050, 0}, {1050, 950, 1050}, {5000, 1100, 5000}, {6000, 1000, 6000}}));
    const auto& c = Session::findTrack(s, t)->clips;
    EXPECT_EQ(c[1].fadeIn.len, 0LL);
    EXPECT_EQ(c[2].fadeOut.len, 100LL);
    EXPECT_EQ(c[3].fadeIn.len, 100LL);
    return true;
}

static bool test_paste_keeps_crossfade_overlap() {
    auto [s, t] = sessionWithClip();
    const std::string a = Session::newClipId(s);
    const std::string b = Session::newClipId(s);
    Session::Track* tr = Session::findTrack(s, t);
    tr->clips.clear();
    Session::Clip ca = makeAudio(s, "a", 0, 1000, 2000);
    ca.id = a;
    ca.fadeOut = {500, Session::FadeShape::Linear, 0.0f};
    Session::Clip cb = makeAudio(s, "b", 0, 2500, 2500);
    cb.id = b;
    cb.fadeIn = {500, Session::FadeShape::Linear, 0.0f};
    tr->clips = {ca, cb};
    Session::sortPlaylist(*tr);
    const auto copied = EngEdit::copyRange(s, t, Time::Range::newRange(0, 6000));
    EXPECT_TRUE(pasteOk(EngEdit::paste(s, t, 20000, copied, 6000, false)));
    std::vector<std::tuple<long long, long long, long long, long long>> pasted;
    for (const auto& c : tr->clips)
        if (c.start >= 20000) pasted.emplace_back(c.start, c.length, c.fadeOut.len, c.fadeIn.len);
    EXPECT_TRUE(expectFadePaste(pasted, {{21000, 2000, 500, 0}, {22500, 2500, 0, 500}}));
    return true;
}

static bool test_trim_clip_bounds_stretched_clip_to_its_file() {
    const std::tuple<double, long long, long long> cases[] = {{0.5, 5000, 5000}, {2.0, 1000, 20000}};
    for (const auto& row : cases) {
        auto [s, t] = sessionWithClip();
        Session::Clip* c = Session::clipMut(s, t, Session::findTrack(s, t)->clips[0].id);
        c->stretch = std::get<0>(row);
        c->start = 0;
        c->length = std::get<1>(row);
        c->offset = 0;
        const std::string id = c->id;
        const auto out = EngEdit::trimClip(s, id, std::nullopt, 99999, false);
        EXPECT_TRUE(out.has_value());
        EXPECT_EQ(ClipOps::end(*out), std::get<2>(row));
    }
    return true;
}

static bool test_trim_clip_start_bound_scales_the_offset_by_stretch() {
    auto [s, t] = sessionWithClip();
    Session::Clip* c = Session::clipMut(s, t, Session::findTrack(s, t)->clips[0].id);
    c->stretch = 2.0;
    c->start = 10000;
    c->length = 1000;
    c->offset = 1000;
    const std::string id = c->id;
    const auto out = EngEdit::trimClip(s, id, 0, std::nullopt, false);
    EXPECT_TRUE(out.has_value());
    EXPECT_EQ(out->start, 8000LL);
    return true;
}

void registerEngineEditTests() {
    Test::add("copy_range_resets_fades_at_cut_edges", test_copy_range_resets_fades_at_cut_edges);
    Test::add("separate_splits_and_heal_rejoins", test_separate_splits_and_heal_rejoins);
    Test::add("heal_rejoins_cut_separation", test_heal_rejoins_cut_separation);
    Test::add("heal_skips_clips_with_different_playback", test_heal_skips_clips_with_different_playback);
    Test::add("heal_keeps_each_side_gain_envelope", test_heal_keeps_each_side_gain_envelope);
    Test::add("heal_keeps_gain_envelopes_across_a_gap", test_heal_keeps_gain_envelopes_across_a_gap);
    Test::add("split_then_heal_rejoins_stretched_clip", test_split_then_heal_rejoins_stretched_clip);
    Test::add("clear_slip_and_shuffle", test_clear_slip_and_shuffle);
    Test::add("remove_clips_closes_each_gap_in_shuffle_only", test_remove_clips_closes_each_gap_in_shuffle_only);
    Test::add("remove_clips_in_shuffle_keeps_crossfade_partners_apart", test_remove_clips_in_shuffle_keeps_crossfade_partners_apart);
    Test::add("copy_paste_overwrites", test_copy_paste_overwrites);
    Test::add("paste_shuffle_ripples", test_paste_shuffle_ripples);
    Test::add("insert_silence_and_trim", test_insert_silence_and_trim);
    Test::add("fades_and_crossfades", test_fades_and_crossfades);
    Test::add("shuffle_moves_reorder_clips_and_keep_the_silence", test_shuffle_moves_reorder_clips_and_keep_the_silence);
    Test::add("shuffling_onto_an_empty_track_starts_it_at_zero", test_shuffling_onto_an_empty_track_starts_it_at_zero);
    Test::add("shuffle_trims_move_later_clips_and_slip_trims_stop_at_neighbours", test_shuffle_trims_move_later_clips_and_slip_trims_stop_at_neighbours);
    Test::add("shuffle_leaves_locked_clips_and_what_follows_them_in_place", test_shuffle_leaves_locked_clips_and_what_follows_them_in_place);
    Test::add("multi_track_moves_keep_the_tracks_apart", test_multi_track_moves_keep_the_tracks_apart);
    Test::add("grid_moves_snap_the_clip_start_or_keep_its_offset", test_grid_moves_snap_the_clip_start_or_keep_its_offset);
    Test::add("moves_overwrite_and_respect_locks", test_moves_overwrite_and_respect_locks);
    Test::add("delete_fades_keeps_playlist_sorted_when_a_clip_overlaps_several", test_delete_fades_keeps_playlist_sorted_when_a_clip_overlaps_several);
    Test::add("delete_fades_leaves_unrelated_overlaps_alone", test_delete_fades_leaves_unrelated_overlaps_alone);
    Test::add("paste_keeps_crossfade_overlap", test_paste_keeps_crossfade_overlap);
    Test::add("trim_clip_bounds_stretched_clip_to_its_file", test_trim_clip_bounds_stretched_clip_to_its_file);
    Test::add("trim_clip_start_bound_scales_the_offset_by_stretch", test_trim_clip_start_bound_scales_the_offset_by_stretch);
}
