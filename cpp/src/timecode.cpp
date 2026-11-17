// Frame rate and timecode conversions.
#include "timecode.h"
#include "time_range.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace Time {

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

int64_t frameNominal(FrameRate r) {
    const FrameRational fr = frameRational(r);
    return (fr.num + fr.den / 2) / fr.den;
}

bool frameIsDrop(FrameRate r) { return r == FrameRate::Fps29_97Drop || r == FrameRate::Fps59_94Drop; }

int64_t framesAt(FrameRate rate, Samples s, SampleRate sr) {
    const FrameRational fr = frameRational(rate);
    const int64_t denI = static_cast<int64_t>(sr.hz) * fr.den;
    if (denI == 0) return 0;
    // after SoundCraft FrameRate::frames_at — i128 div_euclid; use i64 when s * fr.num fits in i64
    if (fr.num > 0 && s > std::numeric_limits<int64_t>::min() / fr.num && s < std::numeric_limits<int64_t>::max() / fr.num) {
        const int64_t numI = s * fr.num;
        int64_t q = numI / denI;
        const int64_t r = numI % denI;
        if (r < 0) q -= 1;
        return q;
    }
    const Int128 num = Int128(s) * fr.num;
    const Int128 den = Int128(static_cast<int64_t>(sr.hz)) * fr.den;
    if (den == Int128(0)) return 0;
    const Int128 q = Int128::divEuclid(num, den);
    if (q > Int128(std::numeric_limits<int64_t>::max()) || q < Int128(std::numeric_limits<int64_t>::min())) return 0;
    return q.saturatingToI64();
}

Samples samplesAtFrame(FrameRate rate, int64_t f, SampleRate sr) {
    const FrameRational fr = frameRational(rate);
    if (fr.num == 0) return 0;
    if (f > -1'000'000'000 && f < 1'000'000'000) {
        // round-half-away; matches SoundCraft i128 path for values that fit in i64
        const double mag = (std::abs(static_cast<double>(f)) * sr.asF64() * static_cast<double>(fr.den) + fr.num / 2.0) /
            static_cast<double>(fr.num);
        const int64_t v = static_cast<int64_t>(mag);
        return f < 0 ? -v : v;
    }
    const Int128 num = Int128::unsignedAbs(f) * static_cast<int64_t>(sr.hz) * fr.den + fr.num / 2;
    Int128 mag = Int128::divEuclid(num, Int128(fr.num));
    if (f < 0) mag = -mag;
    return mag.saturatingToI64();
}

double samplesPerFrame(FrameRate rate, SampleRate sr) {
    const FrameRational fr = frameRational(rate);
    return sr.asF64() * static_cast<double>(fr.den) / static_cast<double>(fr.num);
}

#if defined(_MSC_VER)
__declspec(noinline)
#endif
Timecode Timecode::fromFrames(int64_t frames, FrameRate rate) {
    const bool negative = frames < 0;
    Int128 f = Int128::unsignedAbs(frames);
    const int64_t base = std::max<int64_t>(1, frameNominal(rate));
    if (frameIsDrop(rate)) {
        const int64_t drop = base >= 60 ? 4 : 2;
        const Int128 per10 = Int128(base) * 600 - drop * 9;
        const Int128 perMin = Int128(base) * 60 - drop;
        const Int128 tens = Int128::divEuclid(f, per10);
        const Int128 rem = f - tens * per10;
        Int128 extra = Int128(drop) * 9 * tens;
        if (rem > Int128(drop)) extra = extra + Int128(drop) * Int128::divEuclid(rem - drop, perMin);
        f = f + extra;
    }
    const int64_t framesL = (f - Int128::divEuclid(f, Int128(base)) * base).saturatingToI64();
    const int64_t totalSecs = Int128::divEuclid(f, Int128(base)).saturatingToI64();
    Timecode tc;
    tc.negative = negative;
    tc.hours = std::min<int64_t>(totalSecs / 3600, std::numeric_limits<int64_t>::max());
    tc.minutes = (totalSecs / 60) % 60;
    tc.seconds = totalSecs % 60;
    tc.frames = framesL;
    return tc;
}

int64_t Timecode::toFrames(FrameRate rate) const {
    const int64_t base = std::max<int64_t>(1, frameNominal(rate));
    Int128 totalMin = Int128(hours) * 60 + minutes;
    Int128 f = (totalMin * 60 + seconds) * base + frames;
    if (frameIsDrop(rate)) {
        const int64_t drop = base >= 60 ? 4 : 2;
        f = f - Int128(drop) * (totalMin - Int128::divEuclid(totalMin, Int128(10)));
    }
  if (negative) f = -f;
    return f.saturatingToI64();
}

Timecode Timecode::fromSamples(Samples s, FrameRate rate, SampleRate sr) { return fromFrames(framesAt(rate, s, sr), rate); }

Samples Timecode::toSamples(FrameRate rate, SampleRate sr) const { return samplesAtFrame(rate, toFrames(rate), sr); }

std::string Timecode::formatLabel(FrameRate rate) const {
    const char sep = frameIsDrop(rate) ? ';' : ':';
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%02lld:%02lld:%02lld%c%02lld", negative ? "-" : "", hours, minutes, seconds, sep, frames);
    return buf;
}

bool Timecode::parse(const std::string& text, Timecode& out) {
    std::string t = text;
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
    bool neg = false;
    if (!t.empty() && t.front() == '-') {
        neg = true;
        t = t.substr(1);
    }
    std::vector<int64_t> parts;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty()) return true;
        try {
            size_t idx = 0;
            const int64_t v = std::stoll(cur, &idx);
            if (idx != cur.size() || v < 0) return false;
            parts.push_back(v);
            cur.clear();
            return true;
        } catch (...) {
            return false;
        }
    };
    for (char c : t) {
        if (c == ':' || c == ';' || c == '.') {
            if (!flush()) return false;
        } else if (c == ' ' || c == '\t')
            continue;
        else
            cur.push_back(c);
    }
    if (!flush() || parts.empty() || parts.size() > 4) return false;
    int64_t nums[4] = {0, 0, 0, 0};
    const size_t offset = 4 - parts.size();
    for (size_t i = 0; i < parts.size(); ++i) nums[offset + i] = parts[i];
    out = Timecode{};
    out.negative = neg;
    out.hours = nums[0];
    out.minutes = nums[1];
    out.seconds = nums[2];
    out.frames = nums[3];
    return true;
}

void feetFramesParts(Samples s, FrameRate rate, SampleRate sr, int64_t start, int64_t& feet, int64_t& frames) {
    const int64_t f = Time::satAdd(framesAt(rate, s, sr), start);
    const int64_t d = 16;
    feet = f / d;
    if (f < 0 && f % d)
        feet -= 1;
    frames = f - feet * d;
    if (frames < 0) frames += d;
}

} // namespace Time