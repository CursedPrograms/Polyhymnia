// Sample rate and sample rounding (after SoundCraft crates/time/src/lib.rs).
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace Time {

constexpr int64_t TICKS_PER_QUARTER = 960;
using Samples = int64_t;
constexpr Samples MAX_POSITION = 1LL << 40;

// after SoundCraft (crates/time/src/lib.rs to_samples)
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
    static constexpr uint32_t COMMON[8] = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};

    // after SoundCraft (crates/time/src/lib.rs SampleRate::new)
    static std::optional<SampleRate> tryNew(uint32_t rate) {
        if (rate >= 8000 && rate <= 768000) return SampleRate{rate};
        return std::nullopt;
    }

    static SampleRate fromHz(uint32_t rate) { return {rate < 8000 ? 8000 : (rate > 768000 ? 768000 : rate)}; }

    double asF64() const { return static_cast<double>(hz); }
    double seconds(Samples s) const { return static_cast<double>(s) / asF64(); }
    Samples samples(double seconds) const { return toSamples(seconds * asF64()); }

    bool operator==(const SampleRate& o) const { return hz == o.hz; }
    bool operator!=(const SampleRate& o) const { return hz != o.hz; }
};

} // namespace Time
