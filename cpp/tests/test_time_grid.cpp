// Ports of all 57 tests in SoundCraft crates/time (lib.rs, tempo.rs, timecode.rs, format.rs, grid.rs, tests/it/*.rs).
#include "test_runner.h"
#include "tempo_map.h"
#include "time_format.h"
#include "time_grid.h"
#include "time_range.h"
#include "time_samples.h"
#include "timecode.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using Time::BarBeat;
using Time::FrameRate;
using Time::MeterEvent;
using Time::Range;
using Time::SampleRate;
using Time::TempoEvent;
using Time::TempoMap;
using Time::TICKS_PER_QUARTER;
using Time::Timecode;
using TimeGrid::FeetFramesValue;
using TimeGrid::GridValue;
using TimeGrid::NoteValue;
using TimeGrid::TimecodeValue;

// ============================================================================
// crates/time/src/lib.rs mod tests (4 tests)
// ============================================================================

// after SoundCraft (crates/time/src/lib.rs sample_rate_bounds)
static bool test_sample_rate_bounds() {
    EXPECT_TRUE(SampleRate::tryNew(48000).has_value());
    EXPECT_TRUE(!SampleRate::tryNew(0).has_value());
    EXPECT_TRUE(!SampleRate::tryNew(10000000).has_value());
    return true;
}

// after SoundCraft (crates/time/src/lib.rs seconds_round_trip)
static bool test_seconds_round_trip() {
    const SampleRate sr = SampleRate::HZ_44100();
    EXPECT_EQ(sr.samples(1.0), 44100LL);
    EXPECT_TRUE(std::abs(sr.seconds(22050) - 0.5) < 1e-12);
    return true;
}

// after SoundCraft (crates/time/src/lib.rs to_samples_is_total)
static bool test_to_samples_is_total() {
    EXPECT_EQ(Time::toSamples(std::numeric_limits<double>::quiet_NaN()), 0LL);
    EXPECT_EQ(Time::toSamples(std::numeric_limits<double>::infinity()), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(Time::toSamples(-std::numeric_limits<double>::infinity()), std::numeric_limits<int64_t>::min());
    return true;
}

// after SoundCraft (crates/time/src/lib.rs range_ops)
static bool test_range_ops() {
    const Range a = Range::newRange(10, 0);
    EXPECT_EQ(a, (Range{0, 10}));
    EXPECT_EQ(a.intersectOpt(Range::newRange(5, 20)), std::optional<Range>(Range::newRange(5, 10)));
    EXPECT_TRUE(!a.intersectOpt(Range::newRange(10, 20)).has_value());
    EXPECT_TRUE(a.contains(9) && !a.contains(10));
    return true;
}

// ============================================================================
// crates/time/src/tempo.rs mod tests (16 tests) + tests/it/meter_grid_contract.rs (3 tests)
// ============================================================================

// after SoundCraft (crates/time/src/tempo.rs default_map_is_120_4_4)
static bool test_default_map_is_120_4_4() {
    const TempoMap m = TempoMap::defaultMap();
    const SampleRate sr = SampleRate::HZ_48000();
    EXPECT_EQ(m.samplesAtBarBeat(BarBeat{2, 1, 0}, sr), 96000LL);
    EXPECT_EQ(m.barBeatAt(96000, sr), (BarBeat{2, 1, 0}));
    EXPECT_EQ(m.barBeatAt(24000, sr), (BarBeat{1, 2, 0}));
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs meter_near_max_tick_does_not_overflow)
static bool test_meter_near_max_tick_does_not_overflow() {
    TempoMap m = TempoMap::defaultMap();
    EXPECT_TRUE(m.setMeter(std::numeric_limits<int64_t>::max(), 7, 8));
    const BarBeat bb = m.barBeatAtTick(std::numeric_limits<int64_t>::max());
    EXPECT_TRUE(bb.bar > 1);
    EXPECT_TRUE(m.barStartTick(bb.bar) > 0);
    EXPECT_TRUE(m.barStartTick(std::numeric_limits<int64_t>::max()) > 0);
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs minimum_bar_saturates_instead_of_overflowing)
static bool test_minimum_bar_saturates_instead_of_overflowing() {
    const TempoMap m = TempoMap::defaultMap();
    EXPECT_EQ(m.barStartTick(std::numeric_limits<int64_t>::min()), std::numeric_limits<int64_t>::min());
    EXPECT_EQ(m.barStartTick(0), -TICKS_PER_QUARTER * 4);
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs minimum_beat_is_clamped_without_overflow)
static bool test_minimum_beat_is_clamped_without_overflow() {
    const TempoMap m = TempoMap::defaultMap();
    EXPECT_EQ(m.tickAtBarBeat(BarBeat{1, std::numeric_limits<int64_t>::min(), 0}), 0LL);
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs bar_beat_at_min_tick_does_not_overflow)
static bool test_bar_beat_at_min_tick_does_not_overflow() {
    const TempoMap m = TempoMap::defaultMap();
    const BarBeat bb = m.barBeatAtTick(std::numeric_limits<int64_t>::min());
    EXPECT_TRUE(bb.bar < 1);
    EXPECT_TRUE(bb.beat >= 1 && bb.tick >= 0);
    EXPECT_EQ(m.barBeatAtTick(-1).bar, 0LL);
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs tempo_change_is_integrated)
static bool test_tempo_change_is_integrated() {
    TempoMap m = TempoMap::defaultMap();
    const SampleRate sr = SampleRate::HZ_48000();
    EXPECT_TRUE(m.setTempo(TICKS_PER_QUARTER * 4, 60.0));
    EXPECT_EQ(m.tickToSamples(TICKS_PER_QUARTER * 8, sr), 96000LL + 192000LL);
    EXPECT_EQ(m.samplesToTicks(96000 + 192000, sr), TICKS_PER_QUARTER * 8);
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs meter_change_counts_bars)
static bool test_meter_change_counts_bars() {
    TempoMap m = TempoMap::defaultMap();
    EXPECT_TRUE(m.setMeter(TICKS_PER_QUARTER * 8, 3, 4));
    EXPECT_EQ(m.barBeatAtTick(TICKS_PER_QUARTER * 8), (BarBeat{3, 1, 0}));
    EXPECT_EQ(m.barBeatAtTick(TICKS_PER_QUARTER * 11), (BarBeat{4, 1, 0}));
    EXPECT_EQ(m.barStartTick(4), TICKS_PER_QUARTER * 11);
    const BarBeat bb{4, 2, 10};
    EXPECT_EQ(m.barBeatAtTick(m.tickAtBarBeat(bb)), bb);
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs editing_an_early_meter_keeps_later_bars_and_scales_linearly)
static bool test_editing_an_early_meter_keeps_later_bars_and_scales_linearly() {
    TempoMap m = TempoMap::make(120.0, 4, 4);
    int64_t tick = 0;
    m.metersMut().reserve(200000);
    for (int64_t bar = 1; bar < 200000; ++bar) {
        const uint32_t numerator = static_cast<uint32_t>(3 + (bar % 2));
        tick += m.meters().empty() ? 0 : m.meters().back().ticksPerBar();
        m.metersMut().push_back({tick, numerator, 4});
    }
    EXPECT_EQ(m.barBeatAtTick(m.meters()[1].tick).bar, 2LL);
    EXPECT_EQ(m.barBeatAtTick(m.meters()[1000].tick).bar, 1001LL);
    EXPECT_EQ(m.barBeatAtTick(m.meters()[199998].tick).bar, 199999LL);
    EXPECT_TRUE(m.setMeter(0, 7, 8));
    EXPECT_EQ(m.barBeatAtTick(m.meters()[1].tick).bar, 2LL);
    EXPECT_EQ(m.barBeatAtTick(m.meters()[1000].tick).bar, 1001LL);
    EXPECT_EQ(m.barBeatAtTick(m.meters()[199998].tick).bar, 199999LL);
    EXPECT_EQ(m.meters().size(), 200000u);
    for (size_t i = 0; i + 1 < m.meters().size(); ++i) {
        EXPECT_TRUE(m.meters()[i].tick < m.meters()[i + 1].tick);
    }
    EXPECT_TRUE(m.removeMeter(m.meters()[1].tick));
    EXPECT_EQ(m.barBeatAtTick(m.meters()[1].tick).bar, 3LL);
    EXPECT_EQ(m.barBeatAtTick(m.meters()[999].tick).bar, 1001LL);
    EXPECT_EQ(m.barBeatAtTick(m.meters()[199997].tick).bar, 199999LL);

    TempoMap off = TempoMap::make(120.0, 4, 4);
    off.metersMut().push_back({100, 3, 4});
    off.metersMut().push_back({5000, 5, 4});
    EXPECT_TRUE(off.setMeter(0, 2, 4));
    EXPECT_EQ(off.meters().size(), 3u);
    EXPECT_EQ(off.meters()[0], (MeterEvent{0, 2, 4}));
    EXPECT_EQ(off.meters()[1], (MeterEvent{2 * TICKS_PER_QUARTER, 3, 4}));
    EXPECT_EQ(off.meters()[2], (MeterEvent{8 * TICKS_PER_QUARTER, 5, 4}));
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs eighth_note_meters)
static bool test_eighth_note_meters() {
    const TempoMap m = TempoMap::make(120.0, 6, 8);
    EXPECT_EQ(m.barBeatAtTick(TICKS_PER_QUARTER * 3), (BarBeat{2, 1, 0}));
    EXPECT_EQ(m.barBeatAtTick(TICKS_PER_QUARTER / 2), (BarBeat{1, 2, 0}));
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs sanitize_keeps_what_the_setters_accept)
static bool test_sanitize_keeps_what_the_setters_accept() {
    TempoMap m = TempoMap::fromParts(
        {{960, 4e9}, {3840, 90.0}, {-5, 100.0}, {1920, 60.0}, {1920, 70.0}},
        {{0, 0, 4}, {3840, 3, 4}});
    m.sanitize();
    const std::vector<TempoEvent> wantTempos = {{0, 60.0}, {1920, 60.0}, {3840, 90.0}};
    const std::vector<MeterEvent> wantMeters = {{0, 3, 4}, {3840, 3, 4}};
    EXPECT_EQ(m.tempos(), wantTempos);
    EXPECT_EQ(m.meters(), wantMeters);
    TempoMap empty = TempoMap::fromParts({}, {});
    empty.sanitize();
    EXPECT_EQ(empty, TempoMap::defaultMap());
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs sanitize_drops_events_beyond_the_position_range)
static bool test_sanitize_drops_events_beyond_the_position_range() {
    TempoMap m = TempoMap::fromParts(
        {{0, 120.0}, {std::numeric_limits<int64_t>::max(), 90.0}},
        {{0, 4, 4}, {std::numeric_limits<int64_t>::max(), 3, 4}});
    m.sanitize();
    EXPECT_EQ(m, TempoMap::defaultMap());
    EXPECT_EQ(m.barStartTick(2), TICKS_PER_QUARTER * 4);
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs rejects_bad_values)
static bool test_rejects_bad_values() {
    TempoMap m = TempoMap::defaultMap();
    EXPECT_TRUE(!m.setTempo(0, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_TRUE(!m.setTempo(0, 0.0));
    EXPECT_TRUE(!m.setMeter(0, 0, 4));
    EXPECT_TRUE(!m.setMeter(0, 4, 3));
    EXPECT_TRUE(!m.removeTempo(0));
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs negative_positions)
static bool test_negative_positions() {
    const TempoMap m = TempoMap::defaultMap();
    EXPECT_EQ(m.barBeatAtTick(-TICKS_PER_QUARTER * 4), (BarBeat{0, 1, 0}));
    EXPECT_EQ(m.barBeatAtTick(-TICKS_PER_QUARTER), (BarBeat{0, 4, 0}));
    return true;
}

static int64_t linearTickToSamples(const TempoMap& m, int64_t tick, SampleRate sr) {
    double acc = 0.0;
    int64_t segTick = 0;
    double bpm = m.tempos()[0].bpm;
    for (size_t i = 1; i < m.tempos().size(); ++i) {
        const auto& e = m.tempos()[i];
        if (e.tick >= tick) break;
        acc += static_cast<double>(e.tick - segTick) * TempoMap::samplesPerTick(bpm, sr);
        segTick = e.tick;
        bpm = e.bpm;
    }
    return Time::toSamples(acc + static_cast<double>(tick - segTick) * TempoMap::samplesPerTick(bpm, sr));
}

// after SoundCraft (crates/time/src/tempo.rs indexed_conversions_match_the_segment_walk)
static bool test_indexed_conversions_match_the_segment_walk() {
    const SampleRate sr = SampleRate::HZ_48000();
    TempoMap m = TempoMap::defaultMap();
    for (int64_t i = 1; i < 200; ++i) {
        EXPECT_TRUE(m.setTempo(i * 487, 40.0 + static_cast<double>((i * 37) % 300)));
    }
    EXPECT_TRUE(m.removeTempo(487 * 50));
    for (int64_t tick = -1000; tick < 200 * 487 + 5000; tick += 113) {
        const int64_t s = m.tickToSamples(tick, sr);
        EXPECT_EQ(s, linearTickToSamples(m, tick, sr));
        EXPECT_TRUE(std::abs(m.samplesToTicks(s, sr) - tick) <= 1);
    }
    const TempoMap back = TempoMap::fromParts(m.tempos(), m.meters());
    EXPECT_EQ(back, m);
    EXPECT_EQ(back.tickToSamples(150 * 487 + 3, sr), m.tickToSamples(150 * 487 + 3, sr));
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs a_file_with_wild_ticks_is_indexed_without_overflow)
static bool test_a_file_with_wild_ticks_is_indexed_without_overflow() {
    TempoMap m = TempoMap::fromParts(
        {{0, 120.0}, {std::numeric_limits<int64_t>::min(), 100.0}, {std::numeric_limits<int64_t>::max(), 90.0}},
        {{0, 4, 4}});
    m.sanitize();
    EXPECT_TRUE(!m.tempos().empty());
    EXPECT_EQ(m.tempos().front().tick, 0LL);
    EXPECT_EQ(m.elapsedLen(), m.tempos().size());
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs conversions_against_a_large_tempo_map_stay_fast)
static bool test_conversions_against_a_large_tempo_map_stay_fast() {
    const SampleRate sr = SampleRate::HZ_48000();
    const int64_t n = 200000;
    std::vector<TempoEvent> tempos;
    tempos.reserve(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        tempos.push_back({i * 10, 60.0 + static_cast<double>(i % 7)});
    }
    TempoMap m = TempoMap::fromParts(std::move(tempos), {{0, 4, 4}});
    m.sanitize();
    const auto start = std::chrono::steady_clock::now();
    uint64_t sum = 0;
    for (int64_t i = 0; i < n; ++i) {
        const int64_t s = m.tickToSamples(i * 10 + 5, sr);
        sum += static_cast<uint64_t>(s) + static_cast<uint64_t>(m.samplesToTicks(s, sr));
    }
    EXPECT_TRUE(sum != 0);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    EXPECT_TRUE(elapsedMs < 5000);
    return true;
}

// after SoundCraft (crates/time/tests/it/meter_grid_contract.rs)
static bool assertPositionsRoundTrip(const TempoMap& map) {
    for (int64_t bar = 1; bar <= 5; ++bar) {
        const MeterEvent meter = map.meterAtTick(map.barStartTick(bar));
        for (int64_t beat = 1; beat <= static_cast<int64_t>(meter.numerator); ++beat) {
            const BarBeat position{bar, beat, 0};
            EXPECT_EQ(map.barBeatAtTick(map.tickAtBarBeat(position)), position);
        }
    }
    return true;
}

static std::vector<std::tuple<int64_t, uint32_t, uint32_t>> meterBars(const TempoMap& map) {
    std::vector<std::tuple<int64_t, uint32_t, uint32_t>> out;
    for (const auto& m : map.meters()) {
        const BarBeat bb = map.barBeatAtTick(m.tick);
        out.emplace_back(bb.bar, m.numerator, m.denominator);
    }
    return out;
}

// after SoundCraft (crates/time/tests/it/meter_grid_contract.rs earlier_meter_edit_preserves_bar_positions)
static bool test_earlier_meter_edit_preserves_bar_positions() {
    TempoMap map = TempoMap::defaultMap();
    const int64_t at = map.samplesToTicks(192000, SampleRate::HZ_48000());
    EXPECT_EQ(at, 7680LL);
    EXPECT_TRUE(map.setMeter(at, 3, 4));
    EXPECT_TRUE(assertPositionsRoundTrip(map));
    EXPECT_TRUE(map.setMeter(0, 6, 4));
    EXPECT_TRUE(assertPositionsRoundTrip(map));
    const std::vector<std::tuple<int64_t, uint32_t, uint32_t>> want = {{1, 6, 4}, {3, 3, 4}};
    EXPECT_EQ(meterBars(map), want);
    EXPECT_EQ(map.meters().size(), 2u);
    EXPECT_EQ(map.meters()[1].tick, 2LL * 5760LL);
    return true;
}

// after SoundCraft (crates/time/tests/it/meter_grid_contract.rs inserting_and_removing_meters_keeps_later_bars)
static bool test_inserting_and_removing_meters_keeps_later_bars() {
    TempoMap map = TempoMap::defaultMap();
    EXPECT_TRUE(map.setMeter(map.barStartTick(5), 5, 4));
    EXPECT_TRUE(map.setMeter(map.barStartTick(3), 3, 4));
    const std::vector<std::tuple<int64_t, uint32_t, uint32_t>> want1 = {{1, 4, 4}, {3, 3, 4}, {5, 5, 4}};
    EXPECT_EQ(meterBars(map), want1);
    EXPECT_TRUE(map.setMeter(map.barStartTick(2), 7, 8));
    const std::vector<std::tuple<int64_t, uint32_t, uint32_t>> want2 = {{1, 4, 4}, {2, 7, 8}, {3, 3, 4}, {5, 5, 4}};
    EXPECT_EQ(meterBars(map), want2);
    EXPECT_TRUE(assertPositionsRoundTrip(map));
    int64_t threeFour = 0;
    for (const auto& m : map.meters())
        if (m.numerator == 3) threeFour = m.tick;
    EXPECT_TRUE(map.removeMeter(threeFour));
    const std::vector<std::tuple<int64_t, uint32_t, uint32_t>> want3 = {{1, 4, 4}, {2, 7, 8}, {5, 5, 4}};
    EXPECT_EQ(meterBars(map), want3);
    EXPECT_TRUE(assertPositionsRoundTrip(map));
    return true;
}

// after SoundCraft (crates/time/tests/it/meter_grid_contract.rs initial_meter_first_and_restoring_controls)
static bool test_initial_meter_first_and_restoring_controls() {
    TempoMap map = TempoMap::defaultMap();
    EXPECT_TRUE(map.setMeter(0, 6, 4));
    EXPECT_TRUE(map.setMeter(7680, 3, 4));
    EXPECT_EQ(map.meters().size(), 2u);
    EXPECT_EQ(map.meters()[1], (MeterEvent{5760, 3, 4}));
    EXPECT_TRUE(assertPositionsRoundTrip(map));

    TempoMap map2 = TempoMap::defaultMap();
    EXPECT_TRUE(map2.setMeter(7680, 3, 4));
    EXPECT_TRUE(map2.setMeter(0, 6, 4));
    EXPECT_TRUE(map2.setMeter(0, 4, 4));
    EXPECT_EQ(map2.meters().size(), 2u);
    EXPECT_EQ(map2.meters()[1], (MeterEvent{7680, 3, 4}));
    EXPECT_TRUE(assertPositionsRoundTrip(map2));
    return true;
}

// ============================================================================
// crates/time/src/timecode.rs mod tests (8 tests)
// ============================================================================

// after SoundCraft (crates/time/src/timecode.rs non_drop_round_trip)
static bool test_non_drop_round_trip() {
    const SampleRate sr = SampleRate::HZ_48000();
    const Timecode tc = Timecode::fromSamples(48000LL * 3661LL, FrameRate::Fps25, sr);
    EXPECT_EQ(tc.formatLabel(FrameRate::Fps25), std::string("01:01:01:00"));
    EXPECT_EQ(tc.toSamples(FrameRate::Fps25, sr), 48000LL * 3661LL);
    return true;
}

// after SoundCraft (crates/time/src/timecode.rs negative_timecode_rounds_symmetrically)
static bool test_negative_timecode_rounds_symmetrically() {
    const SampleRate sr = SampleRate::HZ_48000();
    Timecode tc;
    EXPECT_TRUE(Timecode::parse("-00:00:01:00", tc));
    EXPECT_EQ(tc.toSamples(FrameRate::Fps25, sr), -48000LL);
    for (FrameRate r : {FrameRate::Fps25, FrameRate::Fps29_97, FrameRate::Fps29_97Drop, FrameRate::Fps23_976}) {
        for (int64_t f : {1LL, 7LL, 25LL, 1799LL, 107892LL}) {
            EXPECT_EQ(Time::samplesAtFrame(r, -f, sr), -Time::samplesAtFrame(r, f, sr));
        }
    }
    return true;
}

// after SoundCraft (crates/time/src/timecode.rs drop_frame_labels)
static bool test_drop_frame_labels() {
    const FrameRate r = FrameRate::Fps29_97Drop;
    EXPECT_EQ(Timecode::fromFrames(1800, r).formatLabel(r), std::string("00:01:00;02"));
    EXPECT_EQ(Timecode::fromFrames(1799, r).formatLabel(r), std::string("00:00:59;29"));
    EXPECT_EQ(Timecode::fromFrames(17982, r).formatLabel(r), std::string("00:10:00;00"));
    for (int64_t f : {0LL, 1LL, 1799LL, 1800LL, 1801LL, 17981LL, 17982LL, 107892LL, 500000LL}) {
        EXPECT_EQ(Timecode::fromFrames(f, r).toFrames(r), f);
    }
    return true;
}

// after SoundCraft (crates/time/src/timecode.rs huge_timecode_saturates_instead_of_overflowing)
static bool test_huge_timecode_saturates_instead_of_overflowing() {
    const SampleRate sr = SampleRate::HZ_48000();
    for (FrameRate r : {FrameRate::Fps30, FrameRate::Fps29_97Drop}) {
        Timecode tc;
        EXPECT_TRUE(Timecode::parse("9223372036854775807:00:00:00", tc));
        EXPECT_EQ(tc.toSamples(r, sr), std::numeric_limits<int64_t>::max());
        EXPECT_TRUE(Timecode::parse("-9223372036854775807:00:00:00", tc));
        EXPECT_EQ(tc.toSamples(r, sr), std::numeric_limits<int64_t>::min());
    }
    Timecode tc;
    EXPECT_TRUE(Timecode::parse("01:02:03:04", tc));
    EXPECT_EQ(tc.toSamples(FrameRate::Fps30, sr), 178710400LL);
    return true;
}

// after SoundCraft (crates/time/src/timecode.rs extreme_frame_counts_round_trip_at_every_rate)
static bool test_extreme_frame_counts_round_trip_at_every_rate() {
    for (FrameRate rate : Time::FRAME_RATE_ALL) {
        for (int64_t frames : {std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::min() + 1, -1LL, 0LL, 1LL,
                               std::numeric_limits<int64_t>::max() - 1, std::numeric_limits<int64_t>::max()}) {
            const Timecode tc = Timecode::fromFrames(frames, rate);
            EXPECT_EQ(tc.toFrames(rate), frames);
            EXPECT_EQ(tc.negative, frames < 0);
            EXPECT_TRUE(tc.minutes >= 0 && tc.minutes < 60);
            EXPECT_TRUE(tc.seconds >= 0 && tc.seconds < 60);
            EXPECT_TRUE(tc.frames >= 0 && tc.frames < Time::frameNominal(rate));
            Timecode parsed;
            EXPECT_TRUE(Timecode::parse(tc.formatLabel(rate), parsed));
            EXPECT_EQ(parsed, tc);
        }
    }
    return true;
}

// after SoundCraft (crates/time/src/timecode.rs extreme_drop_frame_labels_do_not_overflow)
static bool test_extreme_drop_frame_labels_do_not_overflow() {
    for (FrameRate rate : {FrameRate::Fps29_97Drop, FrameRate::Fps59_94Drop}) {
        EXPECT_EQ(Timecode::fromFrames(std::numeric_limits<int64_t>::max(), rate).toFrames(rate), std::numeric_limits<int64_t>::max());
    }
    return true;
}

// after SoundCraft (crates/time/src/timecode.rs parse_variants)
static bool test_parse_variants() {
    Timecode tc;
    EXPECT_TRUE(Timecode::parse("01:02:03:04", tc));
    EXPECT_EQ(tc.hours, 1LL);
    EXPECT_EQ(tc.minutes, 2LL);
    EXPECT_EQ(tc.seconds, 3LL);
    EXPECT_EQ(tc.frames, 4LL);
    EXPECT_TRUE(Timecode::parse("3:04", tc));
    EXPECT_EQ(tc.seconds, 3LL);
    EXPECT_EQ(tc.frames, 4LL);
    EXPECT_TRUE(!Timecode::parse("a:b", tc));
    EXPECT_TRUE(!Timecode::parse("1:2:3:4:5", tc));
    return true;
}

// after SoundCraft (crates/time/src/timecode.rs feet_and_frames)
static bool test_feet_and_frames() {
    const SampleRate sr = SampleRate::HZ_48000();
    EXPECT_EQ(Time::feetFrames(48000, FrameRate::Fps24, sr, 0), (std::pair<int64_t, int64_t>{1, 8}));
    return true;
}

// ============================================================================
// crates/time/src/format.rs mod tests (8 tests) + tests/it/min_secs_exact_ms.rs (3 tests)
// ============================================================================

// after SoundCraft (crates/time/src/format.rs huge_typed_positions_parse_without_overflowing)
static bool test_huge_typed_positions_parse_without_overflowing() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps30;
    (void)TimeFormat::parsePosition("999999999999999999:0", TimeFormat::Kind::Timecode, sr, map, rate, 0, 0);
    (void)TimeFormat::parsePosition("100000000000000:00:00:00", TimeFormat::Kind::Timecode, sr, map, rate, 0, 0);
    (void)TimeFormat::parsePosition("-9223372036854775808", TimeFormat::Kind::BarsBeats, sr, map, rate, 0, 0);
    return true;
}

// after SoundCraft (crates/time/src/format.rs formats_like_pro_tools_counters)
static bool test_formats_like_pro_tools_counters() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps30;
    EXPECT_EQ(TimeFormat::formatPosition(0, TimeFormat::Kind::MinSecs, sr, map, rate, 0, 0), std::string("0:00.000"));
    EXPECT_EQ(TimeFormat::formatPosition(48000 * 75 + 24000, TimeFormat::Kind::MinSecs, sr, map, rate, 0, 0), std::string("1:15.500"));
    EXPECT_EQ(TimeFormat::formatPosition(0, TimeFormat::Kind::BarsBeats, sr, map, rate, 0, 0), std::string("1| 1| 000"));
    EXPECT_EQ(TimeFormat::formatPosition(48000, TimeFormat::Kind::Timecode, sr, map, rate, 0, 0), std::string("00:00:01:00"));
    EXPECT_EQ(TimeFormat::formatPosition(12345, TimeFormat::Kind::Samples, sr, map, rate, 0, 0), std::string("12345"));
    return true;
}

// after SoundCraft (crates/time/src/format.rs minimum_bar_parses_without_overflow)
static bool test_minimum_bar_parses_without_overflow() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps30;
    auto p = [&](const char* t) { return TimeFormat::parsePosition(t, TimeFormat::Kind::BarsBeats, sr, map, rate, 0, 0); };
    const auto minBar = p("-9223372036854775808|01|000");
    EXPECT_TRUE(minBar.has_value() && *minBar < 0);
    EXPECT_EQ(p("2|3|000"), std::optional<long long>(144000LL));
    EXPECT_EQ(p("0|1|000"), std::optional<long long>(-96000LL));
    return true;
}

// after SoundCraft (crates/time/src/format.rs parse_round_trips)
static bool test_parse_round_trips() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps30;
    for (TimeFormat::Kind fmt : TimeFormat::ALL) {
        for (long long s : {0LL, 48000LL, 96000LL * 3, 1440000LL}) {
            const std::string text = TimeFormat::formatPosition(s, fmt, sr, map, rate, 0, 0);
            const auto back = TimeFormat::parsePosition(text, fmt, sr, map, rate, 0, 0);
            EXPECT_TRUE(back.has_value());
            EXPECT_TRUE(std::abs(*back - s) <= 48);
        }
    }
    return true;
}

// after SoundCraft (crates/time/src/format.rs parse_round_trips_timecode_negative)
static bool test_parse_round_trips_timecode_negative() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps25;
    for (TimeFormat::Kind fmt : {TimeFormat::Kind::Timecode, TimeFormat::Kind::FeetFrames}) {
        for (long long s : {-48000LL, -96000LL * 3, -1440000LL}) {
            const std::string text = TimeFormat::formatPosition(s, fmt, sr, map, rate, 0, 0);
            const auto back = TimeFormat::parsePosition(text, fmt, sr, map, rate, 0, 0);
            EXPECT_EQ(back, std::optional<long long>(s));
        }
    }
    return true;
}

// after SoundCraft (crates/time/src/format.rs parse_rejects_garbage)
static bool test_parse_rejects_garbage() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps30;
    EXPECT_TRUE(!TimeFormat::parsePosition("abc", TimeFormat::Kind::Samples, sr, map, rate, 0, 0).has_value());
    EXPECT_TRUE(!TimeFormat::parsePosition("1:x", TimeFormat::Kind::MinSecs, sr, map, rate, 0, 0).has_value());
    EXPECT_TRUE(!TimeFormat::parsePosition("", TimeFormat::Kind::BarsBeats, sr, map, rate, 0, 0).has_value());
    return true;
}

// after SoundCraft (crates/time/src/format.rs drop_frame_skipped_labels_are_rejected)
static bool test_drop_frame_skipped_labels_are_rejected() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    for (const char* text : {"00:01:00;00", "00:01:00;01"}) {
        EXPECT_TRUE(!TimeFormat::parsePosition(text, TimeFormat::Kind::Timecode, sr, map, FrameRate::Fps29_97Drop, 0, 0).has_value());
    }
    EXPECT_TRUE(TimeFormat::parsePosition("00:10:00;00", TimeFormat::Kind::Timecode, sr, map, FrameRate::Fps29_97Drop, 0, 0).has_value());
    for (const char* text : {"00:01:00;00", "00:01:00;01", "00:01:00;02", "00:01:00;03"}) {
        EXPECT_TRUE(!TimeFormat::parsePosition(text, TimeFormat::Kind::Timecode, sr, map, FrameRate::Fps59_94Drop, 0, 0).has_value());
    }
    EXPECT_TRUE(TimeFormat::parsePosition("00:10:00;00", TimeFormat::Kind::Timecode, sr, map, FrameRate::Fps59_94Drop, 0, 0).has_value());
    return true;
}

// after SoundCraft (crates/time/src/format.rs bar_lengths)
static bool test_bar_lengths() {
    const SampleRate sr = SampleRate::HZ_48000();
    const TempoMap map = TempoMap::defaultMap();
    const FrameRate rate = FrameRate::Fps30;
    EXPECT_EQ(TimeFormat::formatLength(0, 96000 + 24000, TimeFormat::Kind::BarsBeats, sr, map, rate), std::string("1| 1| 000"));
    return true;
}

// after SoundCraft (crates/time/tests/it/min_secs_exact_ms.rs known_positions_at_44100)
static bool test_known_positions_at_44100() {
    const TempoMap map = TempoMap::defaultMap();
    EXPECT_EQ(TimeFormat::formatPosition(88641, TimeFormat::Kind::MinSecs, SampleRate::HZ_44100(), map, FrameRate::Fps24, 0, 0),
              std::string("0:02.010"));
    EXPECT_EQ(TimeFormat::formatPosition(89523, TimeFormat::Kind::MinSecs, SampleRate::HZ_44100(), map, FrameRate::Fps24, 0, 0),
              std::string("0:02.030"));
    return true;
}

// after SoundCraft (crates/time/tests/it/min_secs_exact_ms.rs negative_position_at_44100)
static bool test_negative_position_at_44100() {
    const TempoMap map = TempoMap::defaultMap();
    EXPECT_EQ(TimeFormat::formatPosition(-88641, TimeFormat::Kind::MinSecs, SampleRate::HZ_44100(), map, FrameRate::Fps24, 0, 0),
              std::string("-0:02.010"));
    return true;
}

// after SoundCraft (crates/time/tests/it/min_secs_exact_ms.rs every_exact_millisecond_displays_exactly)
static bool test_every_exact_millisecond_displays_exactly() {
    const TempoMap map = TempoMap::defaultMap();
    for (SampleRate sr : {SampleRate::HZ_44100(), SampleRate::HZ_48000()}) {
        const int64_t hz = static_cast<int64_t>(sr.hz);
        for (int64_t ms = 0; ms <= 600000; ++ms) {
            if ((ms * hz) % 1000 != 0) continue;
            const int64_t s = ms * hz / 1000;
            char expected[32];
            snprintf(expected, sizeof(expected), "%lld:%02lld.%03lld",
                     static_cast<long long>(ms / 60000), static_cast<long long>((ms / 1000) % 60), static_cast<long long>(ms % 1000));
            EXPECT_EQ(TimeFormat::formatPosition(s, TimeFormat::Kind::MinSecs, sr, map, FrameRate::Fps24, 0, 0), std::string(expected));
        }
    }
    return true;
}

// ============================================================================
// crates/time/src/grid.rs mod tests (15 tests)
// ============================================================================

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
                const long long line = Time::samplesAtFrame(rate, Time::divEuclidI64(k, f) * f, sr);
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
    // lib.rs (4)
    Test::add("sample_rate_bounds", test_sample_rate_bounds);
    Test::add("seconds_round_trip", test_seconds_round_trip);
    Test::add("to_samples_is_total", test_to_samples_is_total);
    Test::add("range_ops", test_range_ops);
    // tempo.rs (16) + meter_grid_contract.rs (3)
    Test::add("default_map_is_120_4_4", test_default_map_is_120_4_4);
    Test::add("meter_near_max_tick_does_not_overflow", test_meter_near_max_tick_does_not_overflow);
    Test::add("minimum_bar_saturates_instead_of_overflowing", test_minimum_bar_saturates_instead_of_overflowing);
    Test::add("minimum_beat_is_clamped_without_overflow", test_minimum_beat_is_clamped_without_overflow);
    Test::add("bar_beat_at_min_tick_does_not_overflow", test_bar_beat_at_min_tick_does_not_overflow);
    Test::add("tempo_change_is_integrated", test_tempo_change_is_integrated);
    Test::add("meter_change_counts_bars", test_meter_change_counts_bars);
    Test::add("editing_an_early_meter_keeps_later_bars_and_scales_linearly", test_editing_an_early_meter_keeps_later_bars_and_scales_linearly);
    Test::add("eighth_note_meters", test_eighth_note_meters);
    Test::add("sanitize_keeps_what_the_setters_accept", test_sanitize_keeps_what_the_setters_accept);
    Test::add("sanitize_drops_events_beyond_the_position_range", test_sanitize_drops_events_beyond_the_position_range);
    Test::add("rejects_bad_values", test_rejects_bad_values);
    Test::add("negative_positions", test_negative_positions);
    Test::add("indexed_conversions_match_the_segment_walk", test_indexed_conversions_match_the_segment_walk);
    Test::add("a_file_with_wild_ticks_is_indexed_without_overflow", test_a_file_with_wild_ticks_is_indexed_without_overflow);
    Test::add("conversions_against_a_large_tempo_map_stay_fast", test_conversions_against_a_large_tempo_map_stay_fast);
    Test::add("earlier_meter_edit_preserves_bar_positions", test_earlier_meter_edit_preserves_bar_positions);
    Test::add("inserting_and_removing_meters_keeps_later_bars", test_inserting_and_removing_meters_keeps_later_bars);
    Test::add("initial_meter_first_and_restoring_controls", test_initial_meter_first_and_restoring_controls);
    // timecode.rs (8)
    Test::add("non_drop_round_trip", test_non_drop_round_trip);
    Test::add("negative_timecode_rounds_symmetrically", test_negative_timecode_rounds_symmetrically);
    Test::add("drop_frame_labels", test_drop_frame_labels);
    Test::add("huge_timecode_saturates_instead_of_overflowing", test_huge_timecode_saturates_instead_of_overflowing);
    Test::add("extreme_frame_counts_round_trip_at_every_rate", test_extreme_frame_counts_round_trip_at_every_rate);
    Test::add("extreme_drop_frame_labels_do_not_overflow", test_extreme_drop_frame_labels_do_not_overflow);
    Test::add("parse_variants", test_parse_variants);
    Test::add("feet_and_frames", test_feet_and_frames);
    // format.rs (8) + min_secs_exact_ms.rs (3)
    Test::add("huge_typed_positions_parse_without_overflowing", test_huge_typed_positions_parse_without_overflowing);
    Test::add("formats_like_pro_tools_counters", test_formats_like_pro_tools_counters);
    Test::add("minimum_bar_parses_without_overflow", test_minimum_bar_parses_without_overflow);
    Test::add("parse_round_trips", test_parse_round_trips);
    Test::add("parse_round_trips_timecode_negative", test_parse_round_trips_timecode_negative);
    Test::add("parse_rejects_garbage", test_parse_rejects_garbage);
    Test::add("drop_frame_skipped_labels_are_rejected", test_drop_frame_skipped_labels_are_rejected);
    Test::add("bar_lengths", test_bar_lengths);
    Test::add("known_positions_at_44100", test_known_positions_at_44100);
    Test::add("negative_position_at_44100", test_negative_position_at_44100);
    Test::add("every_exact_millisecond_displays_exactly", test_every_exact_millisecond_displays_exactly);
    // grid.rs (15)
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
