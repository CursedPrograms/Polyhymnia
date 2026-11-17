// Clip helpers ported from SoundCraft model clip.rs.
#include "clip_ops.h"
#include "time_range.h"
#include "time_wide.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace ClipOps {

namespace {

// after SoundCraft (crates/model/src/clip.rs FadeShape::from_id)
std::optional<Session::FadeShape> shapeFromId(const std::string& raw) {
    std::string s;
    s.reserve(raw.size());
    for (unsigned char ch : raw) {
        if (ch == '-' || ch == '_' || ch == ' ') continue;
        s.push_back((char)std::tolower(ch));
    }
    static const struct {
        const char* key;
        Session::FadeShape shape;
    } table[] = {{"linear", Session::FadeShape::Linear},
                 {"equalpower", Session::FadeShape::EqualPower},
                 {"scurve", Session::FadeShape::SCurve},
                 {"exponential", Session::FadeShape::Exponential},
                 {"logarithmic", Session::FadeShape::Logarithmic}};
    for (const auto& row : table) {
        if (s == row.key) return row.shape;
    }
    return std::nullopt;
}

float shapeGain(Session::FadeShape shape, float t) {
    t = std::isnan(t) ? 0.0f : std::clamp(t, 0.0f, 1.0f);
    switch (shape) {
    case Session::FadeShape::Linear: return t;
    case Session::FadeShape::EqualPower: return std::sin(t * 1.5707963267948966f);
    case Session::FadeShape::SCurve: return 0.5f - 0.5f * std::cos(t * 3.141592653589793f);
    case Session::FadeShape::Exponential: return t * t * t;
    case Session::FadeShape::Logarithmic: return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    }
    return t;
}

float fadeGainImpl(const Session::Fade& f, float t) {
    const float base = shapeGain(f.shape, t);
    float curvature = f.curvature;
    if (!std::isfinite(curvature)) curvature = 0.0f;
    curvature = std::clamp(curvature, -1.0f, 1.0f);
    if (curvature == 0.0f || base <= 0.0f || base >= 1.0f) return base;
    const float weighted = base * std::exp2f(curvature * 4.0f);
    return weighted / (weighted + 1.0f - base);
}

namespace {

// after SoundCraft (crates/model/src/clip.rs env_value i128 fractional part)
double int128ToDouble(const Time::Int128& v) {
    if (v.negative()) return -int128ToDouble(-v);
    if (v.hi == 0) return static_cast<double>(v.lo);
    return std::ldexp(static_cast<double>(v.hi), 64) + static_cast<double>(v.lo);
}

double int128Fraction(const Time::Int128& a, const Time::Int128& b) {
    if (b == Time::Int128(0)) return 0.0;
    const double den = int128ToDouble(b);
    return den == 0.0 ? 0.0 : int128ToDouble(a) / den;
}

} // namespace

// after SoundCraft (crates/model/src/clip.rs db_to_gain)
float dbToGain(float db) {
    if (std::isnan(db) || db <= -144.0f) return 0.0f;
    return std::powf(10.0f, std::fminf(db, 48.0f) / 20.0f);
}

// after SoundCraft (crates/model/src/clip.rs env_value)
float envValue(const std::vector<std::pair<long long, float>>& env, long long at) {
    std::optional<std::pair<long long, float>> prev;
    for (const auto& pt : env) {
        if (pt.first >= at) {
            if (prev && pt.first > prev->first) {
                const Time::Int128 elapsed = Time::Int128(at) - Time::Int128(prev->first);
                const Time::Int128 span = Time::Int128(pt.first) - Time::Int128(prev->first);
                if (span > Time::Int128(0)) {
                    return prev->second + (pt.second - prev->second) * static_cast<float>(int128Fraction(elapsed, span));
                }
            }
            return pt.second;
        }
        prev = pt;
    }
    return prev ? prev->second : 0.0f;
}

long long clampPos(long long v) { return std::clamp(v, -kMaxPosition, kMaxPosition); }

} // namespace

// after SoundCraft (crates/model/src/clip.rs Clip::end)
long long end(const Session::Clip& c) { return Time::satAdd(c.start, c.length); }

// after SoundCraft (crates/model/src/clip.rs Clip::range)
Time::Range range(const Session::Clip& c) { return {c.start, end(c)}; }

// after SoundCraft (crates/model/src/clip.rs Clip::source_offset)
long long sourceOffset(const Session::Clip& c) { return c.isMidi ? 0 : c.offset; }

// after SoundCraft (crates/model/src/clip.rs Clip::env_db_at)
float envDbAt(const Session::Clip& c, long long rel) { return envValue(c.gainEnv, rel); }

float gainAtWithFades(const Session::Clip& c, long long rel, Session::Fade fadeIn, Session::Fade fadeOut) {
    float db = c.gainDb;
    if (!c.gainEnv.empty()) db += envDbAt(c, rel);
    float g = dbToGain(db);
    if (fadeIn.len > 0 && rel < fadeIn.len) g *= fadeGainImpl(fadeIn, float(rel) / float(fadeIn.len));
    const long long fromEnd = Time::satSub(c.length, rel);
    if (fadeOut.len > 0 && fromEnd <= fadeOut.len) g *= fadeGainImpl(fadeOut, float(fromEnd) / float(fadeOut.len));
    return g;
}

// after SoundCraft (crates/model/src/clip.rs Clip::gain_at_with_fades)
float gainAt(const Session::Clip& c, long long rel) { return gainAtWithFades(c, rel, c.fadeIn, c.fadeOut); }

float fadeShapeGain(Session::FadeShape shape, float t) { return shapeGain(shape, t); }

float fadeGain(const Session::Fade& f, float t) { return fadeGainImpl(f, t); }

// after SoundCraft (crates/model/src/clip.rs Fade::curvature_for_midpoint_gain)
float fadeCurvatureForMidpointGain(const Session::Fade& f, float gain) {
    if (!std::isfinite(gain)) gain = fadeGain(f, 0.5f);
    gain = std::clamp(gain, 0.001f, 0.999f);
    const float base = shapeGain(f.shape, 0.5f);
    const float out = gain / (1.0f - gain);
    const float ref = base / (1.0f - base);
    return std::clamp((std::log2(out) - std::log2(ref)) / 4.0f, -1.0f, 1.0f);
}

void fadeSanitize(Session::Fade& f) {
    if (!std::isfinite(f.curvature)) f.curvature = 0.0f;
    f.curvature = std::clamp(f.curvature, -1.0f, 1.0f);
}

const char* fadeShapeLabel(Session::FadeShape shape) {
    switch (shape) {
    case Session::FadeShape::Linear: return "Linear";
    case Session::FadeShape::EqualPower: return "Equal Power";
    case Session::FadeShape::SCurve: return "S-Curve";
    case Session::FadeShape::Exponential: return "Exponential";
    case Session::FadeShape::Logarithmic: return "Logarithmic";
    }
    return "Linear";
}

std::optional<Session::FadeShape> fadeShapeFromId(const std::string& s) { return shapeFromId(s); }

// after SoundCraft (crates/model/src/clip.rs Clip::clamp_fades)
void clampFades(Session::Clip& c) {
    c.fadeIn.len = std::clamp(c.fadeIn.len, 0LL, c.length);
    c.fadeOut.len = std::clamp(c.fadeOut.len, 0LL, c.length);
}

// after SoundCraft (crates/model/src/clip.rs Clip::drop_stray_sync)
static void dropStraySync(Session::Clip& c) {
    if (c.syncPoint < 0 || c.syncPoint >= c.length) c.syncPoint = 0;
}

// after SoundCraft (crates/model/src/clip.rs Clip::trim_start_to)
void trimStartTo(Session::Clip& c, long long newStart) {
    const long long clipEnd = end(c);
    long long ns = newStart;
    const long long minStart = Time::satSub(clipEnd, kMaxPosition);
    ns = std::max(ns, minStart);
    ns = clampPos(ns);
    if (clipEnd > 0) {
        const long long maxStart = Time::satAdd(clipEnd, -1);
        ns = std::min(ns, maxStart);
    }
    const long long delta = Time::satAdd(ns, -c.start);
    const double stretch = std::isfinite(c.stretch) && c.stretch > 0.0 ? c.stretch : 1.0;
    if (!c.isMidi) {
        if (std::abs(stretch - 1.0) >= 1e-9)
            c.offset = Time::satAdd(c.offset, (long long)std::llround(double(delta) / stretch));
        else
            c.offset = Time::satAdd(c.offset, delta);
    }
    c.length = std::clamp(Time::satAdd(c.length, -delta), 1LL, kMaxPosition);
    c.start = ns;
    if (c.syncPoint != 0) c.syncPoint = Time::satAdd(c.syncPoint, -delta);
    dropStraySync(c);
    clampFades(c);
    bool hadBefore = false;
    for (const auto& pt : c.gainEnv)
        if (pt.first < delta) {
            hadBefore = true;
            break;
        }
    bool hasAtDelta = false;
    for (const auto& pt : c.gainEnv)
        if (pt.first == delta) {
            hasAtDelta = true;
            break;
        }
    if (hadBefore && !hasAtDelta) c.gainEnv.insert(c.gainEnv.begin(), {delta, envValue(c.gainEnv, delta)});
    c.gainEnv.erase(std::remove_if(c.gainEnv.begin(), c.gainEnv.end(), [&](const auto& p) { return p.first < delta; }), c.gainEnv.end());
    for (auto& pt : c.gainEnv) pt.first = Time::satAdd(pt.first, -delta);
}

// after SoundCraft (crates/model/src/clip.rs Clip::trim_end_to)
void trimEndTo(Session::Clip& c, long long newEnd) {
    long long ne = clampPos(newEnd);
    ne = std::max(ne, c.start + 1);
    c.length = std::clamp(ne - c.start, 1LL, kMaxPosition);
    dropStraySync(c);
    clampFades(c);
}

// after SoundCraft (crates/engine/src/edit.rs locked)
bool locked(const Session::Clip& c) { return c.timeLocked || c.editLocked; }

} // namespace ClipOps