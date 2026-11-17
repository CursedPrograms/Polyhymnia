// SMPTE timecode and frame rates (after SoundCraft crates/time/src/timecode.rs).
#pragma once
#include "time_samples.h"
#include "time_wide.h"

#include <string>
#include <utility>

namespace Time {

enum class FrameRate {
    Fps23_976,
    Fps24,
    Fps25,
    Fps29_97,
    Fps29_97Drop,
    Fps30,
    Fps47_952,
    Fps48,
    Fps50,
    Fps59_94,
    Fps59_94Drop,
    Fps60,
    Fps100,
    Fps119_88,
    Fps120,
};

constexpr FrameRate FRAME_RATE_ALL[15] = {
    FrameRate::Fps23_976,    FrameRate::Fps24,     FrameRate::Fps25,        FrameRate::Fps29_97,
    FrameRate::Fps29_97Drop, FrameRate::Fps30,     FrameRate::Fps47_952,    FrameRate::Fps48,
    FrameRate::Fps50,        FrameRate::Fps59_94,  FrameRate::Fps59_94Drop, FrameRate::Fps60,
    FrameRate::Fps100,       FrameRate::Fps119_88, FrameRate::Fps120,
};

struct FrameRational {
    int64_t num = 30;
    int64_t den = 1;
};

FrameRational frameRational(FrameRate r);
int64_t frameNominal(FrameRate r);
bool frameIsDrop(FrameRate r);
const char* frameLabel(FrameRate r);

struct Timecode {
    bool negative = false;
    int64_t hours = 0;
    int64_t minutes = 0;
    int64_t seconds = 0;
    int64_t frames = 0;

    static Timecode fromFrames(int64_t frames, FrameRate rate);
    int64_t toFrames(FrameRate rate) const;
    static Timecode fromSamples(Samples s, FrameRate rate, SampleRate sr);
    Samples toSamples(FrameRate rate, SampleRate sr) const;

    // after SoundCraft (crates/time/src/timecode.rs Timecode::format / parse)
    std::string formatLabel(FrameRate rate) const;
    static bool parse(const std::string& text, Timecode& out);

    bool operator==(const Timecode& o) const {
        return negative == o.negative && hours == o.hours && minutes == o.minutes && seconds == o.seconds && frames == o.frames;
    }
    bool operator!=(const Timecode& o) const { return !(*this == o); }
};

// after SoundCraft (crates/time/src/timecode.rs feet_frames)
void feetFramesParts(Samples s, FrameRate rate, SampleRate sr, int64_t start, int64_t& feet, int64_t& frames);
std::pair<int64_t, int64_t> feetFrames(Samples s, FrameRate rate, SampleRate sr, int64_t start);

int64_t framesAt(FrameRate rate, Samples s, SampleRate sr);
Samples samplesAtFrame(FrameRate rate, int64_t frame, SampleRate sr);
double samplesPerFrame(FrameRate rate, SampleRate sr);

} // namespace Time
