// Grid and nudge step sizes (after SoundCraft crates/time/src/grid.rs).
#pragma once
#include "tempo_map.h"
#include "timecode.h"
#include "time_samples.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Session {
struct Document;
}

namespace TimeGrid {

enum class NoteValue {
    Bar,
    Whole,
    Half,
    Quarter,
    Eighth,
    Sixteenth,
    ThirtySecond,
    SixtyFourth,
};

const NoteValue NOTE_ALL[8] = {NoteValue::Bar, NoteValue::Whole, NoteValue::Half, NoteValue::Quarter, NoteValue::Eighth,
                               NoteValue::Sixteenth, NoteValue::ThirtySecond, NoteValue::SixtyFourth};

int64_t noteTicks(NoteValue v);
const char* noteLabel(NoteValue v);

enum class GridScale { BarsBeats, MinSecs, Timecode, FeetFrames, Samples, ClipsMarkers };
const GridScale GRID_SCALE_TIME[5] = {GridScale::BarsBeats, GridScale::MinSecs, GridScale::Timecode, GridScale::FeetFrames,
                                      GridScale::Samples};

const char* gridScaleLabel(GridScale s);
const char* gridScaleId(GridScale s);
GridScale gridScaleFromId(const char* id);

enum class TimecodeValue { Second, FiveFrames, Frame, HalfFrame, QuarterFrame, SubFrame };
const TimecodeValue TIMECODE_ALL[6] = {TimecodeValue::Second, TimecodeValue::FiveFrames, TimecodeValue::Frame, TimecodeValue::HalfFrame,
                                       TimecodeValue::QuarterFrame, TimecodeValue::SubFrame};

const char* timecodeValueLabel(TimecodeValue v);
const char* timecodeValueId(TimecodeValue v);

enum class FeetFramesValue { Foot, Frame, QuarterFrame, SubFrame };
const FeetFramesValue FEET_FRAMES_ALL[4] = {FeetFramesValue::Foot, FeetFramesValue::Frame, FeetFramesValue::QuarterFrame,
                                           FeetFramesValue::SubFrame};

const char* feetFramesValueLabel(FeetFramesValue v);

struct GridValue {
    enum class Kind { Note, Seconds, Frames, Timecode, FeetFrames, Samples };
    Kind kind = Kind::Note;
    NoteValue noteVal = NoteValue::Sixteenth;
    bool dotted = false;
    bool triplet = false;
    double secondsVal = 1.0;
    int64_t framesVal = 1;
    TimecodeValue timecodeVal = TimecodeValue::Frame;
    FeetFramesValue feetFramesVal = FeetFramesValue::Frame;
    int64_t samplesVal = 100;

    static GridValue defaultGrid();
    static GridValue forScale(GridScale scale);

    std::string label() const;
    GridScale scale() const;
    GridValue stepped(bool larger) const;

    Time::Samples stepSamples(Time::Samples at, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate) const;
    Time::Samples snap(Time::Samples pos, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate) const;
    Time::Samples lineAtOrBefore(Time::Samples pos, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate) const;
    Time::Samples nextLine(Time::Samples line, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate) const;
    std::vector<Time::Samples> lines(Time::Samples start, Time::Samples end, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate,
                                     size_t max) const;

    // Exposed for parity tests (grid.rs private abs_line / drop_second_frame).
    Time::Samples absLine(int64_t k, Time::SampleRate sr, Time::FrameRate rate) const;
    int64_t absIndexAtOrBefore(Time::Samples pos, Time::SampleRate sr, Time::FrameRate rate) const;
    static int64_t dropSecondFrame(int64_t index, Time::FrameRate rate);

    static GridValue note(NoteValue v, bool dotted, bool triplet);
    static GridValue seconds(double s);
    static GridValue frames(int64_t f);
    static GridValue timecode(TimecodeValue v);
    static GridValue feetFrames(FeetFramesValue v);
    static GridValue samples(int64_t n);

    bool operator==(const GridValue& o) const;
};

// Feet+Frames increment math (after grid.rs FeetFramesValue impl).
Time::Samples feetFramesLineAtOrBefore(FeetFramesValue value, Time::Samples position, Time::SampleRate sr, Time::FrameRate rate, int64_t origin);
Time::Samples feetFramesSnap(FeetFramesValue value, Time::Samples position, Time::SampleRate sr, Time::FrameRate rate, int64_t origin);
std::vector<Time::Samples> feetFramesLines(FeetFramesValue value, Time::Samples start, Time::Samples end, Time::SampleRate sr, Time::FrameRate rate,
                                           int64_t origin, size_t max);
Time::Samples feetFramesNextLine(FeetFramesValue value, Time::Samples position, Time::SampleRate sr, Time::FrameRate rate, int64_t origin);

Time::TempoMap tempoMapFromSession(const Session::Document& doc);
Time::SampleRate sampleRateFromSession(const Session::Document& doc);

long long stepSamples(const Session::Document& doc);
long long gridLineAtOrBeforeExcluding(const Session::Document& doc, long long position, const std::vector<std::string>& excluded);
long long snapToGridExcluding(const Session::Document& doc, long long position, const std::vector<std::string>& excluded);

} // namespace TimeGrid