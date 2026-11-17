// Sample rate and sample rounding (after SoundCraft crates/time lib.rs).
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace Time {

constexpr int64_t TICKS_PER_QUARTER = 960;
using Samples = int64_t;

inline Samples toSamples(double v) {
    if (std::isnan(v)) return 0;
    if (v >= static_cast<double>(std::numeric_limits<int64_t>::max())) return std::numeric_limits<int64_t>::max();
    if (v <= static_cast<double>(std::numeric_limits<int64_t>::min())) return std::numeric_limits<int64_t>::min();
    return static_cast<Samples>(std::llround(v));
}

struct SampleRate {
    uint32_t hz = 48000;

    static constexpr SampleRate HZ_44100() { return {44100}; }
    static constexpr SampleRate HZ_48000() { return {48000}; }

    static SampleRate fromHz(uint32_t rate) { return {rate < 8000 ? 8000 : (rate > 768000 ? 768000 : rate)}; }

    double asF64() const { return static_cast<double>(hz); }
    double seconds(Samples s) const { return static_cast<double>(s) / asF64(); }
    Samples samples(double seconds) const { return toSamples(seconds * asF64()); }
};

} // namespace Time