// Ports of grid.rs tests (after SoundCraft crates/time/src/grid.rs mod tests).
#include "test_runner.h"
#include "tempo_map.h"
#include "time_grid.h"
#include "time_range.h"
#include "timecode.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

using Time::FrameRate;
using Time::SampleRate;
using Time::TempoMap;
using Time::TICKS_PER_QUARTER;
using TimeGrid::FeetFramesValue;
using TimeGrid::GridValue;
using TimeGrid::NoteValue;
using TimeGrid::TimecodeValue;

static bool test_feet_frames_origins_round_only_after_applying_the_frame_offset() {
    const SampleRate sr = SampleRate::HZ_48000();
    const FrameRate rate = FrameRate::Fps29_97;
    for (long long at : {49650LL, 100901LL, 177778LL}) {
        EXPECT_EQ(TimeGrid::feetFramesLineAtOrBefore(FeetFramesValue::Foot, at, sr, rate, 1), at);
        EXPECT_EQ(TimeGrid::feetFramesSnap(FeetFramesValue::Foot, at - 1, sr, rate, 1), at);
    }
    EXPECT_EQ(TimeGrid::feetFramesSnap(FeetFramesValue::QuarterFrame, 50049, sr, rate, 1), 50050);
    const auto lines = TimeGrid::feetFramesLines(FeetFramesValue::SubFrame, 49665, 49667, sr, rate, 1, 4);
    EXPECT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], 49666);
    return true;
}

static bool test_feet_frames_subframes_handle_dense_steps_and_extreme_origins() {
    const SampleRate sr = SampleRate::fromHz(8000);
    const FrameRate rate = FrameRate::Fps120;
    for (long long origin : {std::numeric_limits<long long>::min(), -1LL, 0LL, 1LL, std::numeric_limits<long long>::max()}) {
        const auto got = TimeGrid::feetFramesLines(FeetFramesValue::SubFrame, -3, 4, sr, rate, origin, 20);
        const long long want[] = {-3, -2, -1, 0, 1, 2, 3};
        EXPECT_EQ(got.size(), 7u);
        for (size_t i = 0; i < 7; ++i) EXPECT_EQ(got[i], want[i]);
        for (long long at : {std::numeric_limits<long long>::min(), -1LL, 0LL, 1LL, std::numeric_limits<long long>::max()}) {
            EXPECT_EQ(TimeGrid::feetFramesLineAtOrBefore(FeetFramesValue::SubFrame, at, sr, rate, origin), at);
            EXPECT_EQ(TimeGrid::feetFramesSnap(FeetFramesValue::SubFrame, at, sr, rate, origin), at);
        }
    }
    return true;
}

static bool test_grid_lines_are_their_own_line_at_odd_tempos() {
    const GridValue g = GridValue::defaultGrid();
    const SampleRate sr = SampleRate::HZ_48000();
    const FrameRate rate = FrameRate::Fps30;
    for (double bpm : {90.0, 95.0, 110.0, 130.0, 140.0}) {
        const TempoMap map = TempoMap::make(bpm, 4, 4);
        long long line = 0;
        for (int i = 0; i < 64; ++i) {
            EXPECT_EQ(g.lineAtOrBefore(line, sr, map, rate), line);
            line = g.nextLine(line, sr, map, rate);
        }
    }
    return true;
}

static bool test_grids_stay_aligned_across_a_tempo_change() {
    const SampleRate sr = SampleRate::HZ_48000();
    const FrameRate rate = FrameRate::Fps29_97Drop;
    TempoMap map = TempoMap::defaultMap();
    map.setTempo(TICKS_PER_QUARTER * 4, 90.0);
    const GridValue musical = GridValue::note(NoteValue::Quarter, false, false);
    for (int64_t tick : {0LL, TICKS_PER_QUARTER * 3, TICKS_PER_QUARTER * 4, TICKS_PER_QUARTER * 5}) {
        const long long line = map.tickToSamples(tick, sr);
        EXPECT_EQ(musical.lineAtOrBefore(line, sr, map, rate), line);
    }
    for (GridValue absolute :
         {GridValue::seconds(0.5), GridValue::timecode(TimecodeValue::Second), GridValue::feetFrames(FeetFramesValue::Foot),
          GridValue::samples(1000)}) {
        const long long line = absolute.nextLine(absolute.lineAtOrBefore(100000, sr, map, rate), sr, map, rate);
        EXPECT_EQ(absolute.lineAtOrBefore(line, sr, map, rate), line);
    }
    return true;
}

static bool test_sixteenth_grid_at_120() {
    const GridValue g = GridValue::defaultGrid();
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps30;
    EXPECT_EQ(g.stepSamples(0, sr, map, rate), 6000);
    EXPECT_EQ(g.snap(2900, sr, map, rate), 0);
    EXPECT_EQ(g.snap(3100, sr, map, rate), 6000);
    const auto lines = g.lines(0, 24000, sr, map, rate, 100);
    EXPECT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[0], 0);
    EXPECT_EQ(lines[1], 6000);
    EXPECT_EQ(lines[2], 12000);
    EXPECT_EQ(lines[3], 18000);
    return true;
}

static bool test_absolute_grids() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps25;
    EXPECT_EQ(GridValue::seconds(1.0).snap(70000, sr, map, rate), 48000);
    EXPECT_EQ(GridValue::frames(1).stepSamples(0, sr, map, rate), 1920);
    EXPECT_EQ(GridValue::timecode(TimecodeValue::Second).stepSamples(0, sr, map, rate), 48000);
    EXPECT_EQ(GridValue::timecode(TimecodeValue::FiveFrames).stepSamples(0, sr, map, rate), 9600);
    EXPECT_EQ(GridValue::timecode(TimecodeValue::HalfFrame).stepSamples(0, sr, map, rate), 960);
    EXPECT_EQ(GridValue::timecode(TimecodeValue::QuarterFrame).stepSamples(0, sr, map, rate), 480);
    EXPECT_EQ(GridValue::timecode(TimecodeValue::SubFrame).stepSamples(0, sr, map, rate), 19);
    EXPECT_EQ(GridValue::feetFrames(FeetFramesValue::Foot).stepSamples(0, sr, map, rate), 30720);
    EXPECT_EQ(GridValue::samples(0).stepSamples(0, sr, map, rate), 1);
    return true;
}

static bool test_a_bar_and_a_whole_note_differ_in_three_four() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::make(120.0, 3, 4);
    const FrameRate rate = FrameRate::Fps25;
    auto note = [](NoteValue v) { return GridValue::note(v, false, false); };
    EXPECT_EQ(note(NoteValue::Bar).stepSamples(0, sr, map, rate), 72000);
    EXPECT_EQ(note(NoteValue::Whole).stepSamples(0, sr, map, rate), 96000);
    EXPECT_EQ(note(NoteValue::Bar).lineAtOrBefore(96000, sr, map, rate), 72000);
    EXPECT_EQ(note(NoteValue::Whole).lineAtOrBefore(96000, sr, map, rate), 96000);
    const auto lv = note(NoteValue::Half).lines(0, 150000, sr, map, rate, 10);
    EXPECT_EQ(lv.size(), 5u);
    EXPECT_EQ(lv[0], 0);
    EXPECT_EQ(lv[1], 48000);
    EXPECT_EQ(lv[2], 72000);
    EXPECT_EQ(lv[3], 120000);
    EXPECT_EQ(lv[4], 144000);
    return true;
}

static bool test_seconds_grid_lines_land_on_whole_seconds() {
    const SampleRate sr = SampleRate::HZ_44100();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps30;
    for (GridValue g : {GridValue::seconds(0.001), GridValue::seconds(0.01)}) {
        for (int n = 0; n < 20; ++n) {
            const long long s = n * 44100LL;
            EXPECT_EQ(g.snap(s, sr, map, rate), s);
            EXPECT_EQ(g.lineAtOrBefore(s, sr, map, rate), s);
        }
    }
    return true;
}

static bool test_frame_grid_lines_land_on_frame_starts() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    for (FrameRate rate : {FrameRate::Fps29_97, FrameRate::Fps23_976}) {
        for (int64_t f : {1LL, 10LL}) {
            const GridValue g = GridValue::frames(f);
            for (int64_t k = 0; k < 5000; k += 37) {
                const long long s = Time::samplesAtFrame(rate, k, sr);
                const long long line = Time::samplesAtFrame(rate, (k / f) * f, sr);
                EXPECT_EQ(g.lineAtOrBefore(s, sr, map, rate), line);
                if (k % f == 0) EXPECT_EQ(g.snap(s, sr, map, rate), s);
            }
            const long long start = Time::samplesAtFrame(rate, 4000, sr);
            const long long end = Time::samplesAtFrame(rate, 4000 + 20 * f, sr);
            std::vector<long long> want;
            for (int i = 0; i < 20; ++i) want.push_back(Time::samplesAtFrame(rate, 4000 + i * f, sr));
            const auto got = g.lines(start, end, sr, map, rate, 100);
            EXPECT_EQ(got.size(), want.size());
            for (size_t i = 0; i < want.size(); ++i) EXPECT_EQ(got[i], want[i]);
        }
    }
    return true;
}

static bool test_named_frame_grids_land_on_exact_subdivisions() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    std::vector<GridValue> grids;
    for (TimecodeValue tv : TimeGrid::TIMECODE_ALL) grids.push_back(GridValue::timecode(tv));
    for (FeetFramesValue fv : TimeGrid::FEET_FRAMES_ALL) grids.push_back(GridValue::feetFrames(fv));
    for (FrameRate rate : {FrameRate::Fps29_97, FrameRate::Fps23_976}) {
        for (const GridValue& grid : grids) {
            for (int64_t index = 0; index < 200; ++index) {
                const long long line = grid.absLine(index, sr, rate);
                EXPECT_EQ(grid.lineAtOrBefore(line, sr, map, rate), line);
                EXPECT_EQ(grid.nextLine(line, sr, map, rate), grid.absLine(index + 1, sr, rate));
            }
        }
    }
    return true;
}

static bool test_drop_frame_second_grids_follow_timecode_labels() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const GridValue grid = GridValue::timecode(TimecodeValue::Second);
    for (FrameRate rate : {FrameRate::Fps29_97Drop, FrameRate::Fps59_94Drop}) {
        for (int64_t seconds : {0LL, 59LL, 60LL, 61LL, 600LL, 601LL, 3600LL}) {
            const int64_t frame = GridValue::dropSecondFrame(seconds, rate);
            const long long line = Time::samplesAtFrame(rate, frame, sr);
            EXPECT_EQ(grid.lineAtOrBefore(line, sr, map, rate), line);
            const long long negLine = Time::satSub(0, line);
            EXPECT_EQ(grid.lineAtOrBefore(negLine, sr, map, rate), negLine);
            const int64_t nom = Time::frameNominal(rate);
            for (int64_t offset = -2; offset <= nom + 2; ++offset) {
                const long long position = Time::samplesAtFrame(rate, Time::satAdd(frame, offset), sr);
                const long long before = grid.lineAtOrBefore(position, sr, map, rate);
                EXPECT_TRUE(before <= position);
                EXPECT_TRUE(grid.nextLine(before, sr, map, rate) > position);
            }
        }
        Time::Timecode tc;
        tc.hours = 0;
        tc.minutes = 1;
        tc.seconds = 1;
        tc.frames = 0;
        const long long labelled = tc.toSamples(rate, sr);
        EXPECT_EQ(grid.snap(labelled, sr, map, rate), labelled);
    }
    for (auto pair : std::vector<std::pair<FrameRate, int64_t>>{{FrameRate::Fps29_97Drop, 1800}, {FrameRate::Fps59_94Drop, 3600}}) {
        const FrameRate rate = pair.first;
        const long long minuteFrame = pair.second;
        const long long boundary = Time::samplesAtFrame(rate, minuteFrame, sr);
        EXPECT_EQ(grid.lineAtOrBefore(boundary, sr, map, rate), boundary);
        EXPECT_EQ(grid.snap(boundary, sr, map, rate), boundary);
        const long long negBoundary = Time::satSub(0, boundary);
        EXPECT_EQ(grid.lineAtOrBefore(negBoundary, sr, map, rate), negBoundary);
        EXPECT_EQ(grid.snap(negBoundary, sr, map, rate), negBoundary);
    }
    return true;
}

static bool test_lines_are_capped() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps25;
    const auto lines = GridValue::samples(1).lines(0, 1000000, sr, map, rate, 50);
    EXPECT_EQ(lines.size(), 50u);
    return true;
}

static bool test_triplets_and_dots() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps25;
    const GridValue t = GridValue::note(NoteValue::Quarter, false, true);
    EXPECT_EQ(t.stepSamples(0, sr, map, rate), 16000);
    const GridValue d = GridValue::note(NoteValue::Quarter, true, false);
    EXPECT_EQ(d.stepSamples(0, sr, map, rate), 36000);
    return true;
}

static bool test_values_step_to_the_next_larger_or_smaller() {
    auto note = [](NoteValue v, bool triplet) { return GridValue::note(v, false, triplet); };
    EXPECT_EQ(note(NoteValue::Sixteenth, false).stepped(true), note(NoteValue::Eighth, false));
    EXPECT_EQ(note(NoteValue::Eighth, true).stepped(false), note(NoteValue::Sixteenth, true));
    EXPECT_EQ(note(NoteValue::Bar, false).stepped(true), note(NoteValue::Bar, false));
    EXPECT_EQ(note(NoteValue::SixtyFourth, false).stepped(false), note(NoteValue::SixtyFourth, false));
    EXPECT_EQ(GridValue::seconds(0.01).stepped(true), GridValue::seconds(0.1));
    EXPECT_EQ(GridValue::seconds(0.5).stepped(false), GridValue::seconds(0.1));
    EXPECT_EQ(GridValue::seconds(1.0).stepped(true), GridValue::seconds(1.0));
    EXPECT_EQ(GridValue::frames(1).stepped(true), GridValue::frames(10));
    EXPECT_EQ(GridValue::timecode(TimecodeValue::Frame).stepped(false), GridValue::timecode(TimecodeValue::HalfFrame));
    EXPECT_EQ(GridValue::feetFrames(FeetFramesValue::Frame).stepped(true), GridValue::feetFrames(FeetFramesValue::Foot));
    EXPECT_EQ(GridValue::samples(100).stepped(false), GridValue::samples(10));
    EXPECT_EQ(GridValue::samples(1).stepped(true), GridValue::samples(2));
    EXPECT_EQ(GridValue::samples(1).stepped(false), GridValue::samples(1));
    return true;
}

static bool test_snap_at_extreme_position_does_not_overflow() {
    const SampleRate sr = SampleRate::fromHz(8000);
    const TempoMap map = TempoMap::make(1000.0, 4, 4);
    (void)GridValue::defaultGrid().snap(std::numeric_limits<long long>::max(), sr, map, FrameRate::Fps30);
    return true;
}

void registerTimeGridTests() {
    Test::add("feet_frames_origins_round_only_after_applying_the_frame_offset", test_feet_frames_origins_round_only_after_applying_the_frame_offset);
    Test::add("feet_frames_subframes_handle_dense_steps_and_extreme_origins", test_feet_frames_subframes_handle_dense_steps_and_extreme_origins);
    Test::add("grid_lines_are_their_own_line_at_odd_tempos", test_grid_lines_are_their_own_line_at_odd_tempos);
    Test::add("grids_stay_aligned_across_a_tempo_change", test_grids_stay_aligned_across_a_tempo_change);
    Test::add("sixteenth_grid_at_120", test_sixteenth_grid_at_120);
    Test::add("absolute_grids", test_absolute_grids);
    Test::add("a_bar_and_a_whole_note_differ_in_three_four", test_a_bar_and_a_whole_note_differ_in_three_four);
    Test::add("seconds_grid_lines_land_on_whole_seconds", test_seconds_grid_lines_land_on_whole_seconds);
    Test::add("frame_grid_lines_land_on_frame_starts", test_frame_grid_lines_land_on_frame_starts);
    Test::add("drop_frame_second_grids_follow_timecode_labels", test_drop_frame_second_grids_follow_timecode_labels);
    Test::add("lines_are_capped", test_lines_are_capped);
    Test::add("named_frame_grids_land_on_exact_subdivisions", test_named_frame_grids_land_on_exact_subdivisions);
    Test::add("triplets_and_dots", test_triplets_and_dots);
    Test::add("values_step_to_the_next_larger_or_smaller", test_values_step_to_the_next_larger_or_smaller);
    Test::add("snap_at_extreme_position_does_not_overflow", test_snap_at_extreme_position_does_not_overflow);
}