// Frame rate and timecode conversions (after SoundCraft crates/time/src/timecode.rs).
#include "timecode.h"
#include "time_range.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace Time {

// after SoundCraft (crates/time/src/timecode.rs FrameRate::rational)
FrameRational frameRational(FrameRate r) {
    switch (r) {
    case FrameRate::Fps23_976: return {24000, 1001};
    case FrameRate::Fps24: return {24, 1};
    case FrameRate::Fps25: return {25, 1};
    case FrameRate::Fps29_97:
    case FrameRate::Fps29_97Drop: return {30000, 1001};
    case FrameRate::Fps30: return {30, 1};
    case FrameRate::Fps47_952: return {48000, 1001};
    case FrameRate::Fps48: return {48, 1};
    case FrameRate::Fps50: return {50, 1};
    case FrameRate::Fps59_94:
    case FrameRate::Fps59_94Drop: return {60000, 1001};
    case FrameRate::Fps60: return {60, 1};
    case FrameRate::Fps100: return {100, 1};
    case FrameRate::Fps119_88: return {120000, 1001};
    case FrameRate::Fps120: return {120, 1};
    }
    return {30, 1};
}

// after SoundCraft (crates/time/src/timecode.rs FrameRate::nominal)
int64_t frameNominal(FrameRate r) {
    const FrameRational fr = frameRational(r);
    return (fr.num + fr.den / 2) / fr.den;
}

// after SoundCraft (crates/time/src/timecode.rs FrameRate::is_drop)
bool frameIsDrop(FrameRate r) { return r == FrameRate::Fps29_97Drop || r == FrameRate::Fps59_94Drop; }

// after SoundCraft (crates/time/src/timecode.rs FrameRate::label)
const char* frameLabel(FrameRate r) {
    switch (r) {
    case FrameRate::Fps23_976: return "23.976";
    case FrameRate::Fps24: return "24";
    case FrameRate::Fps25: return "25";
    case FrameRate::Fps29_97: return "29.97";
    case FrameRate::Fps29_97Drop: return "29.97 Drop";
    case FrameRate::Fps30: return "30";
    case FrameRate::Fps47_952: return "47.952";
    case FrameRate::Fps48: return "48";
    case FrameRate::Fps50: return "50";
    case FrameRate::Fps59_94: return "59.94";
    case FrameRate::Fps59_94Drop: return "59.94 Drop";
    case FrameRate::Fps60: return "60";
    case FrameRate::Fps100: return "100";
    case FrameRate::Fps119_88: return "119.88";
    case FrameRate::Fps120: return "120";
    }
    return "24";
}

// after SoundCraft (crates/time/src/timecode.rs FrameRate::frames_at)
int64_t framesAt(FrameRate rate, Samples s, SampleRate sr) {
    const FrameRational fr = frameRational(rate);
    const Int128 num = Int128(s) * fr.num;
    const Int128 den = Int128(static_cast<int64_t>(sr.hz)) * fr.den;
    if (den == Int128(0)) return 0;
    const Int128 q = Int128::divEuclid(num, den);
    if (q > Int128(std::numeric_limits<int64_t>::max()) || q < Int128(std::numeric_limits<int64_t>::min())) return 0;
    return q.saturatingToI64();
}

// after SoundCraft (crates/time/src/timecode.rs FrameRate::samples_at_frame)
Samples samplesAtFrame(FrameRate rate, int64_t f, SampleRate sr) {
    const FrameRational fr = frameRational(rate);
    if (fr.num == 0) return 0;
    const Int128 num = Int128::unsignedAbs(f) * static_cast<int64_t>(sr.hz) * fr.den + fr.num / 2;
    const Int128 mag = num / Int128(fr.num);
    const Int128 v = f < 0 ? -mag : mag;
    return v.saturatingToI64();
}

// after SoundCraft (crates/time/src/timecode.rs FrameRate::samples_per_frame)
double samplesPerFrame(FrameRate rate, SampleRate sr) {
    const FrameRational fr = frameRational(rate);
    return sr.asF64() * static_cast<double>(fr.den) / static_cast<double>(fr.num);
}

// after SoundCraft (crates/time/src/timecode.rs Timecode::from_frames)
Timecode Timecode::fromFrames(int64_t frames, FrameRate rate) {
    const bool negative = frames < 0;
    Int128 f = Int128::unsignedAbs(frames);
    const Int128 base = Int128(std::max<int64_t>(1, frameNominal(rate)));
    if (frameIsDrop(rate)) {
        const int64_t drop = base >= Int128(60) ? 4 : 2;
        const Int128 per10 = base * 600 - Int128(drop * 9);
        const Int128 perMin = base * 60 - Int128(drop);
        const Int128 tens = f / per10;
        const Int128 rem = f % per10;
        Int128 extra = Int128(drop * 9) * tens;
        if (rem > Int128(drop)) extra = extra + Int128(drop) * ((rem - Int128(drop)) / perMin);
        f = f + extra;
    }
    const Int128 framesL = f % base;
    const Int128 totalSecs = f / base;
    Timecode tc;
    tc.negative = negative;
    tc.hours = (totalSecs / Int128(3600)).saturatingToI64();
    tc.minutes = ((totalSecs / Int128(60)) % Int128(60)).saturatingToI64();
    tc.seconds = (totalSecs % Int128(60)).saturatingToI64();
    tc.frames = framesL.saturatingToI64();
    return tc;
}

// after SoundCraft (crates/time/src/timecode.rs Timecode::to_frames)
int64_t Timecode::toFrames(FrameRate rate) const {
    const int64_t base = std::max<int64_t>(1, frameNominal(rate));
    const Int128 totalMin = Int128(hours) * 60 + Int128(minutes);
    Int128 f = (totalMin * 60 + Int128(seconds)) * base + Int128(frames);
    if (frameIsDrop(rate)) {
        const int64_t drop = base >= 60 ? 4 : 2;
        f = f - Int128(drop) * (totalMin - totalMin / Int128(10));
    }
    if (negative) f = -f;
    return f.saturatingToI64();
}

Timecode Timecode::fromSamples(Samples s, FrameRate rate, SampleRate sr) { return fromFrames(framesAt(rate, s, sr), rate); }

Samples Timecode::toSamples(FrameRate rate, SampleRate sr) const { return samplesAtFrame(rate, toFrames(rate), sr); }

// after SoundCraft (crates/time/src/timecode.rs Timecode::format)
std::string Timecode::formatLabel(FrameRate rate) const {
    const char sep = frameIsDrop(rate) ? ';' : ':';
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%02lld:%02lld:%02lld%c%02lld", negative ? "-" : "",
             static_cast<long long>(hours), static_cast<long long>(minutes),
             static_cast<long long>(seconds), sep, static_cast<long long>(frames));
    return buf;
}

static std::string_view trimSv(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return sv;
}

// after SoundCraft (crates/time/src/timecode.rs Timecode::parse)
bool Timecode::parse(const std::string& text, Timecode& out) {
    std::string_view t = trimSv(text);
    bool neg = false;
    if (!t.empty() && t.front() == '-') {
        neg = true;
        t.remove_prefix(1);
    }
    std::vector<std::string_view> parts;
    size_t start = 0;
    for (size_t i = 0; i <= t.size(); ++i) {
        if (i == t.size() || t[i] == ':' || t[i] == ';' || t[i] == '.') {
            parts.push_back(t.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.empty() || parts.size() > 4) return false;
    int64_t nums[4] = {0, 0, 0, 0};
    const size_t offset = 4 - parts.size();
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string_view p = trimSv(parts[i]);
        if (p.empty() || p.front() == '-' || p.front() == '+') return false;
        int64_t v = 0;
        const auto [ptr, ec] = std::from_chars(p.data(), p.data() + p.size(), v);
        if (ec != std::errc{} || ptr != p.data() + p.size() || v < 0) return false;
        nums[offset + i] = v;
    }
    out = Timecode{};
    out.negative = neg;
    out.hours = nums[0];
    out.minutes = nums[1];
    out.seconds = nums[2];
    out.frames = nums[3];
    return true;
}

// after SoundCraft (crates/time/src/timecode.rs feet_frames)
void feetFramesParts(Samples s, FrameRate rate, SampleRate sr, int64_t start, int64_t& feet, int64_t& frames) {
    const int64_t f = Time::satAdd(framesAt(rate, s, sr), start);
    feet = Time::divEuclidI64(f, 16);
    frames = Time::remEuclidI64(f, 16);
}

std::pair<int64_t, int64_t> feetFrames(Samples s, FrameRate rate, SampleRate sr, int64_t start) {
    int64_t ft = 0, fr = 0;
    feetFramesParts(s, rate, sr, start, ft, fr);
    return {ft, fr};
}

} // namespace Time
