// Grid snap and line math (after SoundCraft crates/time/src/grid.rs; session helpers after crates/model/session.rs).
#include "time_grid.h"
#include "clip_ops.h"
#include "session.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace TimeGrid {

namespace {

bool ieqAscii(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}

template <typename T, size_t N>
bool stepEnum(const T (&values)[N], T value, bool larger, T& out) {
    for (size_t i = 0; i < N; ++i)
        if (values[i] == value) {
            if (larger) {
                if (i == 0) return false;
                out = values[i - 1];
                return true;
            }
            if (i + 1 >= N) return false;
            out = values[i + 1];
            return true;
        }
    return false;
}

template <typename T>
bool stepNumber(const T* ladder, size_t count, T value, bool larger, T& out) {
    if (larger) {
        for (size_t i = 0; i < count; ++i)
            if (ladder[i] > value) {
                out = ladder[i];
                return true;
            }
        return false;
    }
    for (size_t i = count; i-- > 0;)
        if (ladder[i] < value) {
            out = ladder[i];
            return true;
        }
    return false;
}

double timecodeValueFrames(TimecodeValue v, Time::FrameRate rate) {
    switch (v) {
    case TimecodeValue::Second: return static_cast<double>(Time::frameNominal(rate));
    case TimecodeValue::FiveFrames: return 5.0;
    case TimecodeValue::Frame: return 1.0;
    case TimecodeValue::HalfFrame: return 0.5;
    case TimecodeValue::QuarterFrame: return 0.25;
    case TimecodeValue::SubFrame: return 0.01;
    }
    return 1.0;
}

void feetFrameRatio(FeetFramesValue v, int64_t& num, int64_t& den) {
    switch (v) {
    case FeetFramesValue::Foot: num = 16; den = 1; break;
    case FeetFramesValue::Frame: num = 1; den = 1; break;
    case FeetFramesValue::QuarterFrame: num = 1; den = 4; break;
    case FeetFramesValue::SubFrame: num = 1; den = 100; break;
    }
}

double feetFramesValueFrames(FeetFramesValue v) {
    int64_t num = 1, den = 1;
    feetFrameRatio(v, num, den);
    return static_cast<double>(num) / static_cast<double>(den);
}

// after SoundCraft (crates/time/src/grid.rs FeetFramesValue::sample_line)
Time::Samples feetSampleLine(FeetFramesValue self, Time::Int128 index, Time::SampleRate sr, Time::FrameRate rate, int64_t origin) {
    int64_t num = 1, den = 1;
    feetFrameRatio(self, num, den);
    const auto fr = Time::frameRational(rate);
    Time::Int128 frames = index * num - Time::Int128(origin) * den;
    Time::Int128 samples = frames * static_cast<int64_t>(std::max<uint32_t>(1, sr.hz)) * fr.den;
    const Time::Int128 divisor = Time::Int128(den) * fr.num;
    if (divisor == Time::Int128(0)) return 0;
    const Time::Int128 halfDiv = divisor / Time::Int128(2);
    const Time::Int128 mag = (samples.abs() + halfDiv) / divisor;
    const Time::Int128 rounded = samples.negative() ? -mag : mag;
    return rounded.saturatingToI64();
}

// after SoundCraft (crates/time/src/grid.rs FeetFramesValue::line_index)
Time::Int128 feetLineIndex(FeetFramesValue self, Time::Samples position, Time::SampleRate sr, Time::FrameRate rate, int64_t origin) {
    int64_t num = 1, den = 1;
    feetFrameRatio(self, num, den);
    const auto fr = Time::frameRational(rate);
    const Time::Int128 scale = Time::Int128(std::max<int64_t>(1, static_cast<int64_t>(sr.hz))) * fr.den;
    Time::Int128 frames = Time::Int128(position) * fr.num + Time::Int128(origin) * scale;
    const Time::Int128 divisor = scale * num;
    Time::Int128 index = 0;
    if (divisor != Time::Int128(0)) index = Time::Int128::divEuclid(frames * Time::Int128(den), divisor);
    if (feetSampleLine(self, index + Time::Int128(1), sr, rate, origin) <= position) index = index + Time::Int128(1);
    return index;
}

int64_t divEuclidI64(int64_t a, int64_t b) {
    if (b <= 0) return 0;
    int64_t q = a / b;
    const int64_t r = a % b;
    if (r < 0) --q;
    return q;
}

int64_t noteTicksGrid(NoteValue value, bool dotted, bool triplet, const Time::TempoMap& map, int64_t atTick) {
    int64_t base = value == NoteValue::Bar ? map.meterAtTick(atTick).ticksPerBar() : noteTicks(value);
    if (dotted) base = base * 3 / 2;
    if (triplet) base = base * 2 / 3;
    return std::max<int64_t>(1, base);
}

double secondsStep(double s, Time::SampleRate sr) {
    const double clamped = std::clamp(s, 0.00001, 3600.0);
    return std::max(1.0, clamped * sr.asF64());
}

} // namespace

int64_t noteTicks(NoteValue v) {
    switch (v) {
    case NoteValue::Bar:
    case NoteValue::Whole: return Time::TICKS_PER_QUARTER * 4;
    case NoteValue::Half: return Time::TICKS_PER_QUARTER * 2;
    case NoteValue::Quarter: return Time::TICKS_PER_QUARTER;
    case NoteValue::Eighth: return Time::TICKS_PER_QUARTER / 2;
    case NoteValue::Sixteenth: return Time::TICKS_PER_QUARTER / 4;
    case NoteValue::ThirtySecond: return Time::TICKS_PER_QUARTER / 8;
    case NoteValue::SixtyFourth: return Time::TICKS_PER_QUARTER / 16;
    }
    return Time::TICKS_PER_QUARTER / 4;
}

const char* noteLabel(NoteValue v) {
    switch (v) {
    case NoteValue::Bar: return "1 bar";
    case NoteValue::Whole: return "Whole note";
    case NoteValue::Half: return "1/2 note";
    case NoteValue::Quarter: return "1/4 note";
    case NoteValue::Eighth: return "1/8 note";
    case NoteValue::Sixteenth: return "1/16 note";
    case NoteValue::ThirtySecond: return "1/32 note";
    case NoteValue::SixtyFourth: return "1/64 note";
    }
    return "1/16 note";
}

const char* gridScaleLabel(GridScale s) {
    switch (s) {
    case GridScale::BarsBeats: return "Bars|Beats";
    case GridScale::MinSecs: return "Min:Secs";
    case GridScale::Timecode: return "Timecode";
    case GridScale::FeetFrames: return "Feet+Frames";
    case GridScale::Samples: return "Samples";
    case GridScale::ClipsMarkers: return "Clips/Markers";
    }
    return "";
}

const char* gridScaleId(GridScale s) {
    switch (s) {
    case GridScale::BarsBeats: return "bars_beats";
    case GridScale::MinSecs: return "min_secs";
    case GridScale::Timecode: return "timecode";
    case GridScale::FeetFrames: return "feet_frames";
    case GridScale::Samples: return "samples";
    case GridScale::ClipsMarkers: return "clips_markers";
    }
    return "";
}

GridScale gridScaleFromId(const char* id) {
    for (GridScale s : GRID_SCALE_TIME)
        if (std::strcmp(id, gridScaleId(s)) == 0 || ieqAscii(id, gridScaleLabel(s))) return s;
    if (std::strcmp(id, gridScaleId(GridScale::ClipsMarkers)) == 0 || ieqAscii(id, gridScaleLabel(GridScale::ClipsMarkers)))
        return GridScale::ClipsMarkers;
    return GridScale::BarsBeats;
}

const char* timecodeValueLabel(TimecodeValue v) {
    switch (v) {
    case TimecodeValue::Second: return "1 second";
    case TimecodeValue::FiveFrames: return "5 frames";
    case TimecodeValue::Frame: return "1 frame";
    case TimecodeValue::HalfFrame: return "1/2 frame";
    case TimecodeValue::QuarterFrame: return "1/4 frame";
    case TimecodeValue::SubFrame: return "1 subframe";
    }
    return "";
}

const char* timecodeValueId(TimecodeValue v) {
    switch (v) {
    case TimecodeValue::Second: return "second";
    case TimecodeValue::FiveFrames: return "5_frames";
    case TimecodeValue::Frame: return "frame";
    case TimecodeValue::HalfFrame: return "half_frame";
    case TimecodeValue::QuarterFrame: return "quarter_frame";
    case TimecodeValue::SubFrame: return "subframe";
    }
    return "";
}

const char* feetFramesValueLabel(FeetFramesValue v) {
    switch (v) {
    case FeetFramesValue::Foot: return "1 foot";
    case FeetFramesValue::Frame: return "1 frame";
    case FeetFramesValue::QuarterFrame: return "1/4 frame";
    case FeetFramesValue::SubFrame: return "1 subframe";
    }
    return "";
}

// Sub-frame step below one sample clamps to 1; every sample is then its own line (SoundCraft grid.rs at low rates).
bool feetFramesDenseSampleLines(FeetFramesValue value, Time::SampleRate sr, Time::FrameRate rate) {
    const double physical = feetFramesValueFrames(value) * Time::samplesPerFrame(rate, sr);
    const Time::Samples stepped = std::max<Time::Samples>(1, Time::toSamples(physical));
    return stepped == 1 && physical < 1.0;
}

Time::Samples feetFramesLineAtOrBefore(FeetFramesValue value, Time::Samples position, Time::SampleRate sr, Time::FrameRate rate, int64_t origin) {
    if (feetFramesDenseSampleLines(value, sr, rate)) return position;
    return feetSampleLine(value, feetLineIndex(value, position, sr, rate, origin), sr, rate, origin);
}

Time::Samples feetFramesNextLine(FeetFramesValue value, Time::Samples position, Time::SampleRate sr, Time::FrameRate rate, int64_t origin) {
    if (feetFramesDenseSampleLines(value, sr, rate)) return Time::satAdd(position, 1);
    const Time::Samples next = feetSampleLine(value, feetLineIndex(value, position, sr, rate, origin) + 1, sr, rate, origin);
    return std::max(next, position + 1);
}

Time::Samples feetFramesSnap(FeetFramesValue value, Time::Samples position, Time::SampleRate sr, Time::FrameRate rate, int64_t origin) {
    if (feetFramesDenseSampleLines(value, sr, rate)) return position;
    const Time::Samples lo = feetFramesLineAtOrBefore(value, position, sr, rate, origin);
    const Time::Samples hi = feetFramesNextLine(value, lo, sr, rate, origin);
    if (Time::satSub(position, lo) <= Time::satSub(hi, position)) return lo;
    return hi;
}

std::vector<Time::Samples> feetFramesLines(FeetFramesValue value, Time::Samples start, Time::Samples end, Time::SampleRate sr, Time::FrameRate rate,
                                            int64_t origin, size_t max) {
    if (feetFramesDenseSampleLines(value, sr, rate)) {
        std::vector<Time::Samples> out;
        for (Time::Samples line = start; line < end && out.size() < max; line = Time::satAdd(line, 1)) {
            if (line >= start) out.push_back(line);
        }
        return out;
    }
    std::vector<Time::Samples> out;
    Time::Samples line = feetFramesLineAtOrBefore(value, start, sr, rate, origin);
    for (size_t guard = 0; line < end && out.size() < max && guard < max + 8; ++guard) {
        if (line >= start) out.push_back(line);
        const Time::Samples next = feetFramesNextLine(value, line, sr, rate, origin);
        if (next <= line) break;
        line = next;
    }
    return out;
}

GridValue GridValue::defaultGrid() { return note(NoteValue::Sixteenth, false, false); }

GridValue GridValue::forScale(GridScale scale) {
    switch (scale) {
    case GridScale::BarsBeats:
    case GridScale::ClipsMarkers: return defaultGrid();
    case GridScale::MinSecs: return seconds(1.0);
    case GridScale::Timecode: return timecode(TimecodeValue::Frame);
    case GridScale::FeetFrames: return feetFrames(FeetFramesValue::Frame);
    case GridScale::Samples: return samples(100);
    }
    return defaultGrid();
}

GridValue GridValue::note(NoteValue v, bool d, bool t) {
    GridValue g;
    g.kind = Kind::Note;
    g.noteVal = v;
    g.dotted = d;
    g.triplet = t;
    return g;
}

GridValue GridValue::seconds(double s) {
    GridValue g;
    g.kind = Kind::Seconds;
    g.secondsVal = s;
    return g;
}
GridValue GridValue::frames(int64_t f) {
    GridValue g;
    g.kind = Kind::Frames;
    g.framesVal = f;
    return g;
}
GridValue GridValue::timecode(TimecodeValue v) {
    GridValue g;
    g.kind = Kind::Timecode;
    g.timecodeVal = v;
    return g;
}
GridValue GridValue::feetFrames(FeetFramesValue v) {
    GridValue g;
    g.kind = Kind::FeetFrames;
    g.feetFramesVal = v;
    return g;
}
GridValue GridValue::samples(int64_t n) {
    GridValue g;
    g.kind = Kind::Samples;
    g.samplesVal = n;
    return g;
}

bool GridValue::operator==(const GridValue& o) const {
    if (kind != o.kind) return false;
    switch (kind) {
    case Kind::Note: return noteVal == o.noteVal && dotted == o.dotted && triplet == o.triplet;
    case Kind::Seconds: return secondsVal == o.secondsVal;
    case Kind::Frames: return framesVal == o.framesVal;
    case Kind::Timecode: return timecodeVal == o.timecodeVal;
    case Kind::FeetFrames: return feetFramesVal == o.feetFramesVal;
    case Kind::Samples: return samplesVal == o.samplesVal;
    }
    return true;
}

std::string GridValue::label() const {
    switch (kind) {
    case Kind::Note: {
        std::string s = noteLabel(noteVal);
        if (dotted) s += " dotted";
        if (triplet) s += " triplet";
        return s;
    }
    case Kind::Seconds:
        if (secondsVal >= 1.0) return std::to_string(static_cast<int>(secondsVal)) + (secondsVal == 1.0 ? " second" : " seconds");
        return std::to_string(static_cast<long long>(std::llround(secondsVal * 1000.0))) + " ms";
    case Kind::Frames: return std::to_string(framesVal) + (framesVal == 1 ? " frame" : " frames");
    case Kind::Timecode: return timecodeValueLabel(timecodeVal);
    case Kind::FeetFrames: return feetFramesValueLabel(feetFramesVal);
    case Kind::Samples: return std::to_string(samplesVal) + (samplesVal == 1 ? " sample" : " samples");
    }
    return {};
}

GridScale GridValue::scale() const {
    switch (kind) {
    case Kind::Note: return GridScale::BarsBeats;
    case Kind::Seconds: return GridScale::MinSecs;
    case Kind::Frames:
    case Kind::Timecode: return GridScale::Timecode;
    case Kind::FeetFrames: return GridScale::FeetFrames;
    case Kind::Samples: return GridScale::Samples;
    }
    return GridScale::BarsBeats;
}

GridValue GridValue::stepped(bool larger) const {
    GridValue out = *this;
    switch (kind) {
    case Kind::Note: {
        for (size_t i = 0; i < 8; ++i) {
            if (NOTE_ALL[i] == noteVal) {
                if (larger) {
                    if (i == 0) return *this;
                    return note(NOTE_ALL[i - 1], dotted, triplet);
                }
                if (i + 1 >= 8) return *this;
                return note(NOTE_ALL[i + 1], dotted, triplet);
            }
        }
        return *this;
    }
    case Kind::Seconds: {
        const double ladder[] = {0.001, 0.01, 0.1, 0.5, 1.0};
        double v;
        if (stepNumber(ladder, 5, secondsVal, larger, v)) return seconds(v);
        return *this;
    }
    case Kind::Frames: {
        const int64_t ladder[] = {1, 10, 100};
        int64_t v;
        if (stepNumber(ladder, 3, framesVal, larger, v)) return frames(v);
        return *this;
    }
    case Kind::Timecode:
        if (stepEnum(TIMECODE_ALL, timecodeVal, larger, out.timecodeVal)) return out;
        return *this;
    case Kind::FeetFrames:
        if (stepEnum(FEET_FRAMES_ALL, feetFramesVal, larger, out.feetFramesVal)) return out;
        return *this;
    case Kind::Samples: {
        const int64_t ladder[] = {1, 2, 10, 100, 1000, 10000};
        int64_t v;
        if (stepNumber(ladder, 6, samplesVal, larger, v)) return samples(v);
        return *this;
    }
    }
    return *this;
}

Time::Samples GridValue::stepSamples(Time::Samples at, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate) const {
    switch (kind) {
    case Kind::Note: {
        const int64_t t0 = map.samplesToTicks(at, sr);
        const int64_t dt = noteTicksGrid(noteVal, dotted, triplet, map, t0);
        const Time::Samples a = map.tickToSamples(t0, sr);
        const Time::Samples b = map.tickToSamples(std::max<int64_t>(0, t0 + dt), sr);
        return std::max<Time::Samples>(1, b - a);
    }
    case Kind::Seconds: return std::max<Time::Samples>(1, sr.samples(std::clamp(secondsVal, 0.00001, 3600.0)));
    case Kind::Frames: return std::max<Time::Samples>(1, Time::toSamples(Time::samplesPerFrame(rate, sr) * std::clamp<int64_t>(framesVal, 1, 1000000)));
    case Kind::Timecode:
        return std::max<Time::Samples>(1, Time::toSamples(Time::samplesPerFrame(rate, sr) * timecodeValueFrames(timecodeVal, rate)));
    case Kind::FeetFrames:
        return std::max<Time::Samples>(1, Time::toSamples(Time::samplesPerFrame(rate, sr) * feetFramesValueFrames(feetFramesVal)));
    case Kind::Samples: return std::max<Time::Samples>(1, samplesVal);
    }
    return 1;
}

// after SoundCraft (crates/time/src/grid.rs GridValue::drop_second_frame index.unsigned_abs)
static uint64_t secondsUnsignedAbs(int64_t index) {
    if (index == std::numeric_limits<int64_t>::min()) return static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ULL;
    return index < 0 ? static_cast<uint64_t>(-index) : static_cast<uint64_t>(index);
}

int64_t GridValue::dropSecondFrame(int64_t index, Time::FrameRate rate) {
    const uint64_t seconds = secondsUnsignedAbs(index);
    const uint64_t totalMinutes = seconds / 60;
    const int64_t nominal = Time::frameNominal(rate);
    int64_t dropFrames = 0;
    if (seconds % 60 == 0 && totalMinutes % 10 != 0) dropFrames = nominal >= 60 ? 4 : 2;
    Time::Timecode tc;
    tc.negative = index < 0;
    tc.hours = static_cast<int64_t>(seconds / 3600);
    tc.minutes = static_cast<int64_t>((seconds / 60) % 60);
    tc.seconds = static_cast<int64_t>(seconds % 60);
    tc.frames = dropFrames;
    return tc.toFrames(rate);
}

bool gridFrameStep(const GridValue& g, Time::FrameRate rate, double& out) {
    switch (g.kind) {
    case GridValue::Kind::Frames:
        out = static_cast<double>(std::clamp<int64_t>(g.framesVal, 1, 1000000));
        return true;
    case GridValue::Kind::Timecode:
        out = timecodeValueFrames(g.timecodeVal, rate);
        return true;
    case GridValue::Kind::FeetFrames:
        out = feetFramesValueFrames(g.feetFramesVal);
        return true;
    default: return false;
    }
}

Time::Samples GridValue::absLine(int64_t k, Time::SampleRate sr, Time::FrameRate rate) const {
    if (kind == Kind::FeetFrames) return feetSampleLine(feetFramesVal, Time::Int128(k), sr, rate, 0);
    if (kind == Kind::Timecode && timecodeVal == TimecodeValue::Second && Time::frameIsDrop(rate))
        return Time::samplesAtFrame(rate, dropSecondFrame(k, rate), sr);
    double frames = 0;
    const bool hasFrame = gridFrameStep(*this, rate, frames);
    if (hasFrame) {
        const double whole = std::round(frames);
        if (frames == whole) return Time::samplesAtFrame(rate, std::max<int64_t>(0, k) * static_cast<int64_t>(whole), sr);
        const double step = std::max(1.0, Time::samplesPerFrame(rate, sr) * frames);
        return Time::toSamples(static_cast<double>(k) * step);
    }
    switch (kind) {
    case Kind::Seconds: return Time::toSamples(static_cast<double>(k) * secondsStep(secondsVal, sr));
    case Kind::Samples: return std::max<int64_t>(0, k) * std::max<int64_t>(1, samplesVal);
    default: return 0;
    }
}

int64_t GridValue::absIndexAtOrBefore(Time::Samples pos, Time::SampleRate sr, Time::FrameRate rate) const {
    if (kind == Kind::Timecode && timecodeVal == TimecodeValue::Second && Time::frameIsDrop(rate)) {
        const Time::Timecode tc = Time::Timecode::fromFrames(Time::framesAt(rate, pos, sr), rate);
        int64_t magnitude = tc.hours;
        magnitude = Time::satAdd(Time::satMul(magnitude, 3600), Time::satAdd(Time::satMul(tc.minutes, 60), tc.seconds));
        // after SoundCraft magnitude.saturating_neg (not 0 - magnitude; MIN negates to MAX)
        int64_t k = tc.negative ? (magnitude == std::numeric_limits<int64_t>::min()
                                        ? std::numeric_limits<int64_t>::max()
                                        : -magnitude)
                                  : magnitude;
        while (absLine(k, sr, rate) > pos) {
            const int64_t previous = Time::satSub(k, 1);
            if (previous == k) break;
            k = previous;
        }
        while (absLine(Time::satAdd(k, 1), sr, rate) <= pos) {
            const int64_t next = Time::satAdd(k, 1);
            if (next == k) break;
            k = next;
        }
        return k;
    }
    double frames = 0;
    const bool hasFrame = gridFrameStep(*this, rate, frames);
    int64_t k = 0;
    if (hasFrame) {
        const double step = std::max(1.0, Time::samplesPerFrame(rate, sr) * frames);
        k = static_cast<int64_t>(std::floor(static_cast<double>(pos) / step));
    } else {
        switch (kind) {
        case Kind::Seconds:
            k = static_cast<int64_t>(std::floor(static_cast<double>(pos) / secondsStep(secondsVal, sr)));
            break;
        case Kind::Samples: {
            const int64_t step = std::max<int64_t>(1, samplesVal);
            return pos / step;
        }
        default: return 0;
        }
    }
    if (absLine(k, sr, rate) > pos) k = std::max<int64_t>(std::numeric_limits<int64_t>::min(), k - 1);
    else if (absLine(k + 1, sr, rate) <= pos) k = k + 1;
    return k;
}

Time::Samples GridValue::lineAtOrBefore(Time::Samples pos, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate) const {
    if (kind == Kind::FeetFrames) return feetFramesLineAtOrBefore(feetFramesVal, pos, sr, rate, 0);
    if (kind == Kind::Note) {
        int64_t t = static_cast<int64_t>(std::floor(map.samplesToTicksF(pos, sr)));
        if (map.tickToSamples(t, sr) > pos) t = std::max<int64_t>(0, t - 1);
        else if (map.tickToSamples(t + 1, sr) <= pos) t = t + 1;
        const Time::BarBeat bb = map.barBeatAtTick(t);
        const int64_t barTick = map.barStartTick(bb.bar);
        const int64_t origin = noteVal == NoteValue::Whole ? 0 : barTick;
        const int64_t step = noteTicksGrid(noteVal, dotted, triplet, map, origin);
        const int64_t k = divEuclidI64(t - origin, step);
        return map.tickToSamples(origin + k * step, sr);
    }
    return absLine(absIndexAtOrBefore(pos, sr, rate), sr, rate);
}

Time::Samples GridValue::nextLine(Time::Samples line, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate) const {
    if (kind == Kind::FeetFrames) return feetFramesNextLine(feetFramesVal, line, sr, rate, 0);
    if (kind != Kind::Note) {
        const Time::Samples next = absLine(absIndexAtOrBefore(line, sr, rate) + 1, sr, rate);
        return next > line ? next : line + 1;
    }
    const Time::Samples step = stepSamples(line, sr, map, rate);
    const Time::Samples candidate = lineAtOrBefore(line + step, sr, map, rate);
    return candidate > line ? candidate : line + step;
}

Time::Samples GridValue::snap(Time::Samples pos, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate) const {
    if (kind == Kind::FeetFrames) return feetFramesSnap(feetFramesVal, pos, sr, rate, 0);
    const Time::Samples lo = lineAtOrBefore(pos, sr, map, rate);
    const Time::Samples hi = nextLine(lo, sr, map, rate);
    return (pos - lo) <= (hi - pos) ? lo : hi;
}

std::vector<Time::Samples> GridValue::lines(Time::Samples start, Time::Samples end, Time::SampleRate sr, const Time::TempoMap& map,
                                              Time::FrameRate rate, size_t max) const {
    if (kind == Kind::FeetFrames) return feetFramesLines(feetFramesVal, start, end, sr, rate, 0, max);
    std::vector<Time::Samples> out;
    Time::Samples p = lineAtOrBefore(start, sr, map, rate);
    for (size_t guard = 0; p < end && out.size() < max && guard < max + 8; ++guard) {
        if (p >= start) out.push_back(p);
        const Time::Samples n = nextLine(p, sr, map, rate);
        if (n <= p) break;
        p = n;
    }
    return out;
}

Time::TempoMap tempoMapFromSession(const Session::Document& doc) { return Time::TempoMap::make(doc.time.tempoBpm, doc.time.meter.num, doc.time.meter.den); }

Time::SampleRate sampleRateFromSession(const Session::Document& doc) { return Time::SampleRate::fromHz(static_cast<uint32_t>(doc.sampleRate)); }

static std::vector<long long> clipMarkerPoints(const Session::Document& doc, const std::vector<std::string>& excluded, bool includeZero) {
    std::unordered_set<std::string> ex(excluded.begin(), excluded.end());
    std::vector<long long> points;
    if (includeZero) points.push_back(0);
    for (const auto& track : doc.tracks) {
        if (!Session::trackShown(doc, track)) continue;
        for (const auto& clip : track.clips) {
            if (ex.count(clip.id)) continue;
            points.push_back(clip.start);
            points.push_back(ClipOps::end(clip));
        }
    }
    for (const auto& m : doc.markers) points.push_back(m.start);
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

static Time::Samples gridOrigin(const Session::Document& doc) {
    if (doc.edit.grid.scale() == GridScale::Timecode) return doc.timecodeStart;
    return 0;
}

long long stepSamples(const Session::Document& doc) {
    return doc.edit.grid.stepSamples(0, sampleRateFromSession(doc), tempoMapFromSession(doc), doc.frameRate);
}

long long gridLineAtOrBeforeExcluding(const Session::Document& doc, long long position, const std::vector<std::string>& excluded) {
    if (doc.edit.gridClipsMarkers) {
        long long best = position;
        bool any = false;
        for (long long p : clipMarkerPoints(doc, excluded, true)) {
            if (p <= position) {
                best = p;
                any = true;
            }
        }
        return any ? best : position;
    }
    const auto sr = sampleRateFromSession(doc);
    const auto map = tempoMapFromSession(doc);
  if (doc.edit.grid.kind == GridValue::Kind::FeetFrames)
        return feetFramesLineAtOrBefore(doc.edit.grid.feetFramesVal, position, sr, doc.frameRate, doc.feetFramesStart);
    const Time::Samples origin = gridOrigin(doc);
    return doc.edit.grid.lineAtOrBefore(position + origin, sr, map, doc.frameRate) - origin;
}

long long snapToGridExcluding(const Session::Document& doc, long long position, const std::vector<std::string>& excluded) {
    if (doc.edit.gridClipsMarkers) {
        const auto pts = clipMarkerPoints(doc, excluded, true);
        if (pts.empty()) return position;
        const auto it = std::lower_bound(pts.begin(), pts.end(), position);
        long long before = pts.front();
        long long after = pts.back();
        if (it != pts.begin()) before = *std::prev(it);
        if (it != pts.end()) after = *it;
        if (it == pts.begin()) after = *it;
        return (position - before <= after - position) ? before : after;
    }
    const auto sr = sampleRateFromSession(doc);
    const auto map = tempoMapFromSession(doc);
    if (doc.edit.grid.kind == GridValue::Kind::FeetFrames)
        return feetFramesSnap(doc.edit.grid.feetFramesVal, position, sr, doc.frameRate, doc.feetFramesStart);
    const Time::Samples origin = gridOrigin(doc);
    return doc.edit.grid.snap(position + origin, sr, map, doc.frameRate) - origin;
}

} // namespace TimeGrid