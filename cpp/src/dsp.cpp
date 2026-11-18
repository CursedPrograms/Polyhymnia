// Polyhymnia - Built-in DSP effects, biquad filters, and plugin registry (after SoundCraft crates/dsp/src/*).
#include "dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kPiF = 3.14159265358979323846f;
constexpr float kTauF = 6.28318530717958647692f;
constexpr float kInvSqrt2 = 0.70710678118654752440f;

float saneSr(float sr) {
    if (!std::isfinite(sr)) return 48000.0f;
    return std::clamp(sr, 8000.0f, 384000.0f);
}

size_t saneChannels(size_t ch) {
    return std::clamp<size_t>(ch, 1, 16);
}

float flushDenorm(float x) {
    if (!std::isfinite(x) || std::abs(x) < 1e-20f) return 0.0f;
    return x;
}

float timeCoef(float ms, float sr) {
    if (ms <= 0.0f || sr <= 0.0f) return 0.0f;
    return std::exp(-1000.0f / (ms * sr));
}

float msToSamples(float ms, float sr) {
    return std::max(0.0f, ms * 0.001f * sr);
}

ParamInfo linParam(const char* id, const char* name, float mn, float mx, float def, Unit u) {
    return {id, name, mn, mx, def, u, Taper::Linear, {}};
}

ParamInfo logParam(const char* id, const char* name, float mn, float mx, float def, Unit u) {
    return {id, name, mn, mx, def, u, Taper::Log, {}};
}

ParamInfo toggleParam(const char* id, const char* name, bool def) {
    return {id, name, 0.0f, 1.0f, def ? 1.0f : 0.0f, Unit::Toggle, Taper::Linear, {}};
}

ParamInfo choiceParam(const char* id, const char* name, std::vector<const char*> choices, int defIdx) {
    const float mx = choices.empty() ? 0.0f : static_cast<float>(choices.size() - 1);
    return {id, name, 0.0f, mx, static_cast<float>(defIdx), Unit::Choice, Taper::Linear, std::move(choices)};
}

// Delay line with linear/cubic interpolation (after SoundCraft crates/dsp/src/util.rs DelayLine)
struct DelayLine {
    std::vector<float> buf;
    size_t pos = 0;

    void allocate(size_t len) {
        buf.assign(std::max<size_t>(4, len + 4), 0.0f);
        pos = 0;
    }
    void clear() {
        std::fill(buf.begin(), buf.end(), 0.0f);
        pos = 0;
    }
    size_t maxDelay() const {
        return buf.size() > 4 ? buf.size() - 4 : 1;
    }
    void push(float x) {
        if (buf.empty()) return;
        buf[pos] = flushDenorm(x);
        pos = (pos + 1) % buf.size();
    }
    float tap(size_t delaySamples) const {
        if (buf.empty()) return 0.0f;
        const size_t d = std::min(delaySamples, buf.size() - 1);
        const size_t idx = (pos + buf.size() - d) % buf.size();
        return buf[idx];
    }
    float readLinear(float delaySamples) const {
        if (buf.empty()) return 0.0f;
        const float d = std::clamp(delaySamples, 1.0f, static_cast<float>(buf.size() - 2));
        const size_t i0 = static_cast<size_t>(d);
        const float frac = d - static_cast<float>(i0);
        const float a = tap(i0);
        const float b = tap(i0 + 1);
        return a + (b - a) * frac;
    }
};

struct DcBlocker {
    float r = 0.995f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    void setCutoff(float hz, float sr) {
        r = std::clamp(1.0f - kTauF * hz / std::max(1.0f, sr), 0.8f, 0.9999f);
    }
    void reset() {
        x1 = 0.0f;
        y1 = 0.0f;
    }
    float process(float x) {
        const float y = flushDenorm(x - x1 + r * y1);
        x1 = x;
        y1 = y;
        return y;
    }
};

} // namespace

// after SoundCraft (crates/dsp/src/lib.rs db_to_gain)
float dbToGain(float db) {
    if (std::isnan(db) || db <= kMinDb) return 0.0f;
    return std::pow(10.0f, std::min(db, 200.0f) / 20.0f);
}

// after SoundCraft (crates/dsp/src/lib.rs gain_to_db)
float gainToDb(float g) {
    const float a = std::abs(g);
    if (std::isnan(a)) return kMinDb;
    if (std::isinf(a)) return 3.402823466e+38f;
    return std::max(20.0f * std::log10(std::max(a, 1e-20f)), kMinDb);
}

// after SoundCraft (crates/dsp/src/lib.rs midi_to_hz)
double midiToHz(double note) {
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

// after SoundCraft (crates/dsp/src/plugins/dynamics.rs compress_gr)
float compressGr(float levelDb, float threshold, float ratio, float knee) {
    const float slope = 1.0f - 1.0f / std::max(ratio, 1.0f);
    const float over = levelDb - threshold;
    if (knee > 1e-3f && 2.0f * std::abs(over) <= knee) {
        const float t = over + knee * 0.5f;
        return slope * t * t / (2.0f * knee);
    } else if (over > 0.0f) {
        return slope * over;
    }
    return 0.0f;
}

const char* categoryLabel(Category c) {
    switch (c) {
        case Category::Eq: return "EQ";
        case Category::Dynamics: return "Dynamics";
        case Category::PitchShift: return "Pitch Shift";
        case Category::Reverb: return "Reverb";
        case Category::Delay: return "Delay";
        case Category::Modulation: return "Modulation";
        case Category::Harmonic: return "Harmonic";
        case Category::Dither: return "Dither";
        case Category::Instrument: return "Instrument";
        case Category::Other: return "Other";
    }
    return "Other";
}

// after SoundCraft (crates/dsp/src/lib.rs ParamInfo::clamp)
float ParamInfo::clamp(float value) const {
    float v = std::isnan(value) ? defaultVal : value;
    v = std::clamp(v, minVal, maxVal);
    if (unit == Unit::Toggle || unit == Unit::Choice) {
        v = std::clamp(std::round(v), minVal, maxVal);
    }
    return v;
}

// after SoundCraft (crates/dsp/src/lib.rs ParamInfo::to_normalized)
float ParamInfo::toNormalized(float value) const {
    const float v = clamp(value);
    const float span = maxVal - minVal;
    if (span <= 0.0f) return 0.0f;
    if (taper == Taper::Log && minVal > 0.0f) {
        return std::clamp(std::log(v / minVal) / std::log(maxVal / minVal), 0.0f, 1.0f);
    }
    return std::clamp((v - minVal) / span, 0.0f, 1.0f);
}

// after SoundCraft (crates/dsp/src/lib.rs ParamInfo::from_normalized)
float ParamInfo::fromNormalized(float t) const {
    const float tn = std::isnan(t) ? 0.0f : std::clamp(t, 0.0f, 1.0f);
    float v = 0.0f;
    if (taper == Taper::Log && minVal > 0.0f) {
        v = minVal * std::pow(maxVal / minVal, tn);
    } else {
        v = minVal + (maxVal - minVal) * tn;
    }
    return clamp(v);
}

// after SoundCraft (crates/dsp/src/lib.rs ParamInfo::format)
std::string ParamInfo::format(float value) const {
    const float v = clamp(value);
    char buf[64];
    switch (unit) {
        case Unit::Choice: {
            const size_t idx = static_cast<size_t>( std::max(0.0f, std::round(v)));
            if (idx < choices.size()) return choices[idx];
            std::snprintf(buf, sizeof(buf), "%.0f", v);
            return buf;
        }
        case Unit::Toggle:
            return v >= 0.5f ? "On" : "Off";
        case Unit::Db:
            if (v <= -95.9f) return "-inf dB";
            std::snprintf(buf, sizeof(buf), "%.1f dB", v);
            return buf;
        case Unit::Hz:
            if (v >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.2f kHz", v / 1000.0f);
            else std::snprintf(buf, sizeof(buf), "%.1f Hz", v);
            return buf;
        case Unit::Ms:
            std::snprintf(buf, sizeof(buf), "%.1f ms", v);
            return buf;
        case Unit::Percent:
            std::snprintf(buf, sizeof(buf), "%.0f%%", v);
            return buf;
        case Unit::Ratio:
            std::snprintf(buf, sizeof(buf), "%.1f:1", v);
            return buf;
        case Unit::Semitones:
            std::snprintf(buf, sizeof(buf), "%.0f st", v);
            return buf;
        case Unit::Cents:
            std::snprintf(buf, sizeof(buf), "%.0f ct", v);
            return buf;
        case Unit::Seconds:
            std::snprintf(buf, sizeof(buf), "%.2f s", v);
            return buf;
        case Unit::None:
            std::snprintf(buf, sizeof(buf), "%.2f", v);
            return buf;
    }
    return "";
}

const ParamInfo* PluginInfo::param(const std::string& paramId) const {
    for (const auto& p : params) {
        if (paramId == p.id) return &p;
    }
    return nullptr;
}

// after SoundCraft (crates/dsp/src/biquad.rs Coeffs::design)
Coeffs Coeffs::design(FilterType kind, float freq, float q, float gainDb, float sampleRate) {
    const double sr = static_cast<double>(saneSr(sampleRate));
    const double f = std::isfinite(freq) ? std::clamp(static_cast<double>(freq), 1.0, sr * 0.49) : 1000.0;
    const double qv = std::isfinite(q) ? std::clamp(static_cast<double>(q), 0.025, 100.0) : 0.7071067811865475;
    const double g = std::isfinite(gainDb) ? std::clamp(static_cast<double>(gainDb), -48.0, 48.0) : 0.0;
    const double w0 = 2.0 * kPi * f / sr;
    const double sw = std::sin(w0);
    const double cw = std::cos(w0);
    const double alpha = sw / (2.0 * qv);
    const double a = std::pow(10.0, g / 40.0);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
    switch (kind) {
        case FilterType::LowPass:
            b0 = (1.0 - cw) / 2.0;
            b1 = 1.0 - cw;
            b2 = (1.0 - cw) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha;
            break;
        case FilterType::HighPass:
            b0 = (1.0 + cw) / 2.0;
            b1 = -(1.0 + cw);
            b2 = (1.0 + cw) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha;
            break;
        case FilterType::BandPass:
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha;
            break;
        case FilterType::Notch:
            b0 = 1.0;
            b1 = -2.0 * cw;
            b2 = 1.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha;
            break;
        case FilterType::AllPass:
            b0 = 1.0 - alpha;
            b1 = -2.0 * cw;
            b2 = 1.0 + alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha;
            break;
        case FilterType::Peak:
            b0 = 1.0 + alpha * a;
            b1 = -2.0 * cw;
            b2 = 1.0 - alpha * a;
            a0 = 1.0 + alpha / a;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha / a;
            break;
        case FilterType::LowShelf: {
            const double s = 2.0 * std::sqrt(a) * alpha;
            b0 = a * ((a + 1.0) - (a - 1.0) * cw + s);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cw);
            b2 = a * ((a + 1.0) - (a - 1.0) * cw - s);
            a0 = (a + 1.0) + (a - 1.0) * cw + s;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cw);
            a2 = (a + 1.0) + (a - 1.0) * cw - s;
            break;
        }
        case FilterType::HighShelf: {
            const double s = 2.0 * std::sqrt(a) * alpha;
            b0 = a * ((a + 1.0) + (a - 1.0) * cw + s);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cw);
            b2 = a * ((a + 1.0) + (a - 1.0) * cw - s);
            a0 = (a + 1.0) - (a - 1.0) * cw + s;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cw);
            a2 = (a + 1.0) - (a - 1.0) * cw - s;
            break;
        }
        case FilterType::LowPass1:
        case FilterType::HighPass1: {
            const double k = std::tan(w0 / 2.0);
            const double norm = 1.0 / (1.0 + k);
            const double a1v = (k - 1.0) * norm;
            if (kind == FilterType::LowPass1) {
                return {static_cast<float>(k * norm), static_cast<float>(k * norm), 0.0f, static_cast<float>(a1v), 0.0f};
            } else {
                return {static_cast<float>(norm), static_cast<float>(-norm), 0.0f, static_cast<float>(a1v), 0.0f};
            }
        }
    }
    Coeffs c{
        static_cast<float>(b0 / a0),
        static_cast<float>(b1 / a0),
        static_cast<float>(b2 / a0),
        static_cast<float>(a1 / a0),
        static_cast<float>(a2 / a0),
    };
    if (std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2) && std::isfinite(c.a1) && std::isfinite(c.a2)) {
        return c;
    }
    return Coeffs::identity();
}

// after SoundCraft (crates/dsp/src/biquad.rs Coeffs::magnitude_db)
float Coeffs::magnitudeDb(float freq, float sampleRate) const {
    const double sr = static_cast<double>(saneSr(sampleRate));
    const double f = std::isfinite(freq) ? std::clamp(static_cast<double>(freq), 0.0, sr * 0.5) : 0.0;
    const double w = 2.0 * kPi * f / sr;
    const double s1 = std::sin(w), c1 = std::cos(w);
    const double s2 = std::sin(2.0 * w), c2 = std::cos(2.0 * w);
    const double nr = b0 + b1 * c1 + b2 * c2;
    const double ni = -(b1 * s1 + b2 * s2);
    const double dr = 1.0 + a1 * c1 + a2 * c2;
    const double di = -(a1 * s1 + a2 * s2);
    const double num = nr * nr + ni * ni;
    const double den = std::max(dr * dr + di * di, 1e-30);
    const double db = 10.0 * std::log10(std::max(num / den, 1e-30));
    return static_cast<float>(std::max(db, -300.0));
}

Biquad Biquad::design(FilterType kind, float freq, float q, float gainDb, float sampleRate) {
    Biquad b;
    b.coeffs = Coeffs::design(kind, freq, q, gainDb, sampleRate);
    return b;
}

void Biquad::set(FilterType kind, float freq, float q, float gainDb, float sampleRate) {
    coeffs = Coeffs::design(kind, freq, q, gainDb, sampleRate);
}

void Biquad::reset() {
    z1 = 0.0f;
    z2 = 0.0f;
}

float Biquad::processSample(float x) {
    const float y = coeffs.b0 * x + z1;
    z1 = flushDenorm(coeffs.b1 * x - coeffs.a1 * y + z2);
    z2 = flushDenorm(coeffs.b2 * x - coeffs.a2 * y);
    if (std::isfinite(y)) return y;
    reset();
    return 0.0f;
}

void Biquad::processBlock(float* buf, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        buf[i] = processSample(buf[i]);
    }
}

float Biquad::magnitudeDb(float freq, float sampleRate) const {
    return coeffs.magnitudeDb(freq, sampleRate);
}

namespace {

// ============================================================================
// Built-in Plugin Metadata Definitions (all 24 plugins from crates/dsp/src/plugins.rs)
// ============================================================================

const std::vector<const char*> kSlopes = {"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct"};
const std::vector<const char*> kEq1Types = {"Peak", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass"};

const PluginInfo kEq1Info = {
    "eq_1band", "EQ 1-Band", "EQ1", Category::Eq,
    {
        choiceParam("type", "Type", kEq1Types, 0),
        logParam("freq", "Freq", 20.0f, 20000.0f, 1000.0f, Unit::Hz),
        linParam("gain", "Gain", -24.0f, 24.0f, 0.0f, Unit::Db),
        logParam("q", "Q", 0.1f, 10.0f, 1.0f, Unit::None),
        linParam("output_gain", "Output", -24.0f, 24.0f, 0.0f, Unit::Db),
    },
    false, true
};

const PluginInfo kEq7Info = {
    "eq_7band", "EQ 7-Band", "EQ7", Category::Eq,
    {
        linParam("input_gain", "Input", -24.0f, 24.0f, 0.0f, Unit::Db),
        toggleParam("hpf_on", "HPF", false),
        logParam("hpf_freq", "HPF Freq", 10.0f, 2000.0f, 80.0f, Unit::Hz),
        choiceParam("hpf_slope", "HPF Slope", kSlopes, 1),
        logParam("low_shelf_freq", "LF Freq", 20.0f, 1000.0f, 100.0f, Unit::Hz),
        linParam("low_shelf_gain", "LF Gain", -24.0f, 24.0f, 0.0f, Unit::Db),
        logParam("low_shelf_q", "LF Q", 0.3f, 2.0f, 0.707f, Unit::None),
        logParam("low_mid_freq", "LMF Freq", 20.0f, 2000.0f, 250.0f, Unit::Hz),
        linParam("low_mid_gain", "LMF Gain", -24.0f, 24.0f, 0.0f, Unit::Db),
        logParam("low_mid_q", "LMF Q", 0.1f, 10.0f, 1.0f, Unit::None),
        logParam("mid_freq", "MF Freq", 100.0f, 8000.0f, 1000.0f, Unit::Hz),
        linParam("mid_gain", "MF Gain", -24.0f, 24.0f, 0.0f, Unit::Db),
        logParam("mid_q", "MF Q", 0.1f, 10.0f, 1.0f, Unit::None),
        logParam("high_mid_freq", "HMF Freq", 500.0f, 20000.0f, 4000.0f, Unit::Hz),
        linParam("high_mid_gain", "HMF Gain", -24.0f, 24.0f, 0.0f, Unit::Db),
        logParam("high_mid_q", "HMF Q", 0.1f, 10.0f, 1.0f, Unit::None),
        logParam("high_shelf_freq", "HF Freq", 1000.0f, 20000.0f, 8000.0f, Unit::Hz),
        linParam("high_shelf_gain", "HF Gain", -24.0f, 24.0f, 0.0f, Unit::Db),
        logParam("high_shelf_q", "HF Q", 0.3f, 2.0f, 0.707f, Unit::None),
        toggleParam("lpf_on", "LPF", false),
        logParam("lpf_freq", "LPF Freq", 1000.0f, 20000.0f, 18000.0f, Unit::Hz),
        choiceParam("lpf_slope", "LPF Slope", kSlopes, 1),
        linParam("output_gain", "Output", -24.0f, 24.0f, 0.0f, Unit::Db),
    },
    false, true
};

const PluginInfo kCompInfo = {
    "compressor", "Compressor/Limiter", "Comp", Category::Dynamics,
    {
        linParam("threshold", "Threshold", -60.0f, 0.0f, -20.0f, Unit::Db),
        logParam("ratio", "Ratio", 1.0f, 100.0f, 4.0f, Unit::Ratio),
        logParam("attack", "Attack", 0.01f, 300.0f, 10.0f, Unit::Ms),
        logParam("release", "Release", 5.0f, 3000.0f, 100.0f, Unit::Ms),
        linParam("knee", "Knee", 0.0f, 24.0f, 6.0f, Unit::Db),
        linParam("makeup", "Makeup", 0.0f, 40.0f, 0.0f, Unit::Db),
        toggleParam("sc_hpf_on", "SC HPF", false),
        logParam("sc_hpf_freq", "SC HPF Freq", 20.0f, 500.0f, 100.0f, Unit::Hz),
        toggleParam("limiter", "Limit", false),
        linParam("lookahead", "Lookahead", 0.0f, 10.0f, 0.0f, Unit::Ms),
        linParam("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent),
    },
    false, true
};

const PluginInfo kGateInfo = {
    "expander_gate", "Expander/Gate", "Gate", Category::Dynamics,
    {
        linParam("threshold", "Threshold", -80.0f, 0.0f, -40.0f, Unit::Db),
        logParam("ratio", "Ratio", 1.0f, 100.0f, 100.0f, Unit::Ratio),
        linParam("range", "Range", 0.0f, 80.0f, 80.0f, Unit::Db),
        logParam("attack", "Attack", 0.01f, 100.0f, 0.5f, Unit::Ms),
        linParam("hold", "Hold", 0.0f, 2000.0f, 50.0f, Unit::Ms),
        logParam("release", "Release", 1.0f, 4000.0f, 100.0f, Unit::Ms),
    },
    false, true
};

const PluginInfo kDeEssInfo = {
    "de_esser", "De-Esser", "De-Ess", Category::Dynamics,
    {
        logParam("freq", "Frequency", 2000.0f, 16000.0f, 6500.0f, Unit::Hz),
        linParam("range", "Range", 0.0f, 24.0f, 10.0f, Unit::Db),
        linParam("threshold", "Threshold", -60.0f, 0.0f, -24.0f, Unit::Db),
        toggleParam("listen", "Listen", false),
    },
    false, true
};

const PluginInfo kMaxInfo = {
    "maximizer", "Maximizer", "Max", Category::Dynamics,
    {
        linParam("threshold", "Threshold", -30.0f, 0.0f, 0.0f, Unit::Db),
        linParam("ceiling", "Ceiling", -30.0f, 0.0f, -0.3f, Unit::Db),
        logParam("release", "Release", 1.0f, 1000.0f, 50.0f, Unit::Ms),
    },
    false, true
};

const PluginInfo kStripInfo = {
    "channel_strip", "Channel Strip", "Strip", Category::Dynamics,
    {
        linParam("input_gain", "Input", -24.0f, 24.0f, 0.0f, Unit::Db),
        toggleParam("gate_on", "Gate", false),
        linParam("gate_threshold", "Gate Thresh", -80.0f, 0.0f, -50.0f, Unit::Db),
        linParam("gate_range", "Gate Range", 0.0f, 80.0f, 40.0f, Unit::Db),
        logParam("gate_release", "Gate Release", 1.0f, 4000.0f, 150.0f, Unit::Ms),
        toggleParam("comp_on", "Comp", false),
        linParam("comp_threshold", "Comp Thresh", -60.0f, 0.0f, -18.0f, Unit::Db),
        logParam("comp_ratio", "Comp Ratio", 1.0f, 100.0f, 3.0f, Unit::Ratio),
        logParam("comp_attack", "Comp Attack", 0.01f, 300.0f, 10.0f, Unit::Ms),
        logParam("comp_release", "Comp Release", 5.0f, 3000.0f, 120.0f, Unit::Ms),
        linParam("comp_makeup", "Comp Makeup", 0.0f, 40.0f, 0.0f, Unit::Db),
        toggleParam("eq_on", "EQ", true),
        toggleParam("hpf_on", "HPF", false),
        logParam("hpf_freq", "HPF Freq", 10.0f, 2000.0f, 80.0f, Unit::Hz),
        linParam("low_gain", "Low Gain", -24.0f, 24.0f, 0.0f, Unit::Db),
        logParam("low_freq", "Low Freq", 20.0f, 1000.0f, 100.0f, Unit::Hz),
        linParam("mid_gain", "Mid Gain", -24.0f, 24.0f, 0.0f, Unit::Db),
        logParam("mid_freq", "Mid Freq", 100.0f, 8000.0f, 1000.0f, Unit::Hz),
        logParam("mid_q", "Mid Q", 0.1f, 10.0f, 1.0f, Unit::None),
        linParam("high_gain", "High Gain", -24.0f, 24.0f, 0.0f, Unit::Db),
        logParam("high_freq", "High Freq", 1000.0f, 20000.0f, 8000.0f, Unit::Hz),
        linParam("output_gain", "Output", -24.0f, 24.0f, 0.0f, Unit::Db),
    },
    false, true
};

const PluginInfo kRoomInfo = {
    "room_reverb", "Room Reverb", "Room", Category::Reverb,
    {
        linParam("mix", "Mix", 0.0f, 100.0f, 25.0f, Unit::Percent),
        linParam("pre_delay", "Pre-Delay", 0.0f, 250.0f, 10.0f, Unit::Ms),
        logParam("decay", "Decay", 0.1f, 20.0f, 1.2f, Unit::Seconds),
        linParam("size", "Size", 0.0f, 100.0f, 50.0f, Unit::Percent),
        logParam("damping", "HF Damping", 1000.0f, 20000.0f, 6000.0f, Unit::Hz),
        linParam("diffusion", "Diffusion", 0.0f, 100.0f, 70.0f, Unit::Percent),
        linParam("width", "Width", 0.0f, 100.0f, 100.0f, Unit::Percent),
        logParam("low_cut", "Low Cut", 20.0f, 1000.0f, 20.0f, Unit::Hz),
    },
    false, true
};

const PluginInfo kPlateInfo = {
    "plate_reverb", "Plate Reverb", "Plate", Category::Reverb,
    {
        linParam("mix", "Mix", 0.0f, 100.0f, 30.0f, Unit::Percent),
        linParam("pre_delay", "Pre-Delay", 0.0f, 250.0f, 20.0f, Unit::Ms),
        logParam("decay", "Decay", 0.1f, 20.0f, 2.5f, Unit::Seconds),
        linParam("size", "Size", 0.0f, 100.0f, 50.0f, Unit::Percent),
        logParam("damping", "HF Damping", 1000.0f, 20000.0f, 10000.0f, Unit::Hz),
        linParam("diffusion", "Diffusion", 0.0f, 100.0f, 85.0f, Unit::Percent),
        linParam("width", "Width", 0.0f, 100.0f, 100.0f, Unit::Percent),
        logParam("low_cut", "Low Cut", 20.0f, 1000.0f, 20.0f, Unit::Hz),
    },
    false, true
};

const PluginInfo kDelayInfo = {
    "mod_delay", "Mod Delay", "ModDly", Category::Delay,
    {
        logParam("time", "Time", 1.0f, 2000.0f, 250.0f, Unit::Ms),
        linParam("feedback", "Feedback", -99.0f, 99.0f, 30.0f, Unit::Percent),
        linParam("mix", "Mix", 0.0f, 100.0f, 30.0f, Unit::Percent),
        logParam("mod_rate", "Mod Rate", 0.01f, 10.0f, 0.5f, Unit::Hz),
        linParam("mod_depth", "Mod Depth", 0.0f, 100.0f, 0.0f, Unit::Percent),
        logParam("lpf", "LPF", 500.0f, 20000.0f, 12000.0f, Unit::Hz),
    },
    false, true
};

const PluginInfo kAutoPanInfo = {
    "auto_pan", "Auto Pan", "Auto Pan", Category::Modulation,
    {
        linParam("output", "Output", -24.0f, 12.0f, 0.0f, Unit::Db),
        linParam("width", "Width", 0.0f, 100.0f, 100.0f, Unit::Percent),
        linParam("manual", "Manual", -100.0f, 100.0f, 0.0f, Unit::Percent),
        choiceParam("source", "Source", {"LFO", "Envelope"}, 0),
        choiceParam("path", "Path", {"Left to Right", "Right to Left"}, 0),
        logParam("rate", "Rate (BPM)", 1.0f, 960.0f, 120.0f, Unit::None),
        choiceParam("waveform", "Waveform", {"Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Half Sine", "4 Step Triangle"}, 0),
        choiceParam("trigger", "Trigger", {"Free", "Single", "Beat Clock", "Envelope", "Manual"}, 0),
        toggleParam("retrigger", "Manual Trigger", false),
        toggleParam("link_tempo", "Link to Tempo", false),
        choiceParam("duration", "Duration", {"16th Note", "8th Note", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars"}, 4),
        linParam("threshold", "Threshold", -60.0f, -1.0f, -30.0f, Unit::Db),
        logParam("attack", "Attack", 0.1f, 100.0f, 5.0f, Unit::Ms),
        logParam("release", "Release", 5.0f, 2000.0f, 150.0f, Unit::Ms),
    },
    false, true
};

const PluginInfo kChorusInfo = {
    "chorus", "Chorus", "Chorus", Category::Modulation,
    {
        logParam("rate", "Rate", 0.05f, 5.0f, 0.8f, Unit::Hz),
        linParam("depth", "Depth", 0.0f, 100.0f, 50.0f, Unit::Percent),
        linParam("delay", "Delay", 5.0f, 30.0f, 12.0f, Unit::Ms),
        linParam("feedback", "Feedback", 0.0f, 90.0f, 0.0f, Unit::Percent),
        linParam("mix", "Mix", 0.0f, 100.0f, 50.0f, Unit::Percent),
        linParam("spread", "Spread", 0.0f, 100.0f, 100.0f, Unit::Percent),
    },
    false, true
};

const PluginInfo kFlangerInfo = {
    "flanger", "Flanger", "Flanger", Category::Modulation,
    {
        logParam("rate", "Rate", 0.02f, 5.0f, 0.25f, Unit::Hz),
        linParam("depth", "Depth", 0.0f, 100.0f, 70.0f, Unit::Percent),
        linParam("delay", "Delay", 0.1f, 10.0f, 2.0f, Unit::Ms),
        linParam("feedback", "Feedback", -95.0f, 95.0f, 50.0f, Unit::Percent),
        linParam("mix", "Mix", 0.0f, 100.0f, 50.0f, Unit::Percent),
    },
    false, true
};

const PluginInfo kPhaserInfo = {
    "phaser", "Phaser", "Phaser", Category::Modulation,
    {
        logParam("rate", "Rate", 0.02f, 5.0f, 0.5f, Unit::Hz),
        linParam("depth", "Depth", 0.0f, 100.0f, 70.0f, Unit::Percent),
        choiceParam("stages", "Stages", {"2", "4", "6", "8", "12"}, 2),
        linParam("feedback", "Feedback", -95.0f, 95.0f, 40.0f, Unit::Percent),
        logParam("center", "Center", 100.0f, 4000.0f, 800.0f, Unit::Hz),
        linParam("mix", "Mix", 0.0f, 100.0f, 50.0f, Unit::Percent),
    },
    false, true
};

const PluginInfo kRingInfo = {
    "ring_mod", "Ring Modulator", "RingMod", Category::Modulation,
    {
        choiceParam("effect", "Effect", {"Ring", "Frequency Mod", "Resonator +", "Resonator -"}, 0),
        logParam("frequency", "Frequency", 20.0f, 5000.0f, 440.0f, Unit::Hz),
        linParam("resonance", "Resonance", 0.0f, 95.0f, 50.0f, Unit::Percent),
        choiceParam("mod_source", "Mod Source", {"LFO", "Envelope", "Sample & Hold"}, 0),
        logParam("mod_rate", "Mod Rate", 0.05f, 20.0f, 1.0f, Unit::Hz),
        linParam("mod_depth", "Mod Depth", 0.0f, 100.0f, 0.0f, Unit::Percent),
        linParam("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent),
    },
    false, true
};

const PluginInfo kSatInfo = {
    "saturator", "Saturator", "Satur", Category::Harmonic,
    {
        linParam("drive", "Drive", 0.0f, 36.0f, 6.0f, Unit::Db),
        linParam("bias", "Asymmetry", 0.0f, 100.0f, 0.0f, Unit::Percent),
        logParam("tone", "Tone", 1000.0f, 20000.0f, 20000.0f, Unit::Hz),
        linParam("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent),
        linParam("output", "Output", -24.0f, 12.0f, 0.0f, Unit::Db),
    },
    false, true
};

const PluginInfo kLoFiInfo = {
    "lofi", "Bit Crusher", "Crush", Category::Harmonic,
    {
        linParam("bits", "Bit Depth", 1.0f, 24.0f, 8.0f, Unit::None),
        logParam("sample_rate", "Sample Rate", 500.0f, 48000.0f, 11025.0f, Unit::Hz),
        linParam("noise", "Noise", 0.0f, 100.0f, 0.0f, Unit::Percent),
        linParam("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent),
    },
    false, true
};

const PluginInfo kRectInfo = {
    "rectifier", "Rectifier", "Rectify", Category::Harmonic,
    {
        choiceParam("mode", "Mode", {"Half Wave", "Full Wave"}, 1),
        linParam("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent),
        linParam("output", "Output", -24.0f, 12.0f, 0.0f, Unit::Db),
    },
    false, true
};

const PluginInfo kSubInfo = {
    "subharmonic", "Subharmonic", "SubHarm", Category::Harmonic,
    {
        linParam("input", "Input", -18.0f, 18.0f, 0.0f, Unit::Db),
        choiceParam("range", "Range", {"120-180 Hz", "80-120 Hz", "60-90 Hz", "40-60 Hz"}, 2),
        linParam("lower_gain", "Lower Gain", -120.0f, 24.0f, 0.0f, Unit::Db),
        linParam("upper_gain", "Upper Gain", -120.0f, 24.0f, -6.0f, Unit::Db),
        linParam("direct_gain", "Direct Gain", -120.0f, 24.0f, -120.0f, Unit::Db),
        toggleParam("lower_solo", "Lower Solo", false),
        toggleParam("upper_solo", "Upper Solo", false),
        toggleParam("direct_solo", "Direct Solo", false),
        linParam("drive", "Drive", -18.0f, 18.0f, 0.0f, Unit::Db),
        choiceParam("character", "Character", {"Clean I", "Clean II", "Resonant I", "Resonant II", "Distort I", "Distort II"}, 0),
        logParam("hp_freq", "HP Freq", 5.0f, 1000.0f, 20.0f, Unit::Hz),
        logParam("hp_q", "HP Q", 0.1f, 5.0f, 0.707f, Unit::None),
        logParam("lp_freq", "LP Freq", 5.0f, 1000.0f, 200.0f, Unit::Hz),
        logParam("lp_q", "LP Q", 0.1f, 5.0f, 0.707f, Unit::None),
        linParam("mix", "Mix", 0.0f, 100.0f, 50.0f, Unit::Percent),
        toggleParam("wet_solo", "Wet Solo", false),
        linParam("output", "Output", -18.0f, 18.0f, 0.0f, Unit::Db),
    },
    false, true
};

const PluginInfo kPitchInfo = {
    "pitch_shifter", "Pitch Shifter", "Pitch", Category::PitchShift,
    {
        linParam("semitones", "Semitones", -24.0f, 24.0f, 0.0f, Unit::Semitones),
        linParam("cents", "Fine", -100.0f, 100.0f, 0.0f, Unit::Cents),
        linParam("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent),
    },
    false, true
};

const PluginInfo kShiftInfo = {
    "time_shift", "Time Shift", "TShift", Category::Other,
    {
        linParam("delay_ms", "Delay", 0.0f, 1000.0f, 0.0f, Unit::Ms),
        linParam("delay_samples", "Samples", 0.0f, 10000.0f, 0.0f, Unit::None),
    },
    false, false
};

const PluginInfo kGainInfo = {
    "gain", "Gain", "Gain", Category::Other,
    {
        linParam("gain", "Gain", -96.0f, 24.0f, 0.0f, Unit::Db),
    },
    false, true
};

const PluginInfo kTrimInfo = {
    "trim", "Trim", "Trim", Category::Other,
    {
        linParam("gain", "Trim", -96.0f, 12.0f, 0.0f, Unit::Db),
        toggleParam("invert", "Phase Invert", false),
    },
    false, true
};

const PluginInfo kInvertInfo = {
    "invert", "Invert", "Invert", Category::Other, {}, false, true
};

const PluginInfo kDcInfo = {
    "dc_offset_removal", "DC Offset Removal", "DC Rmv", Category::Other,
    {
        logParam("cutoff", "Cutoff", 1.0f, 40.0f, 5.0f, Unit::Hz),
    },
    false, true
};

const PluginInfo kGenInfo = {
    "signal_generator", "Signal Generator", "SigGen", Category::Other,
    {
        choiceParam("waveform", "Waveform", {"Sine", "Square", "Saw", "Triangle", "White Noise", "Pink Noise"}, 0),
        logParam("freq", "Frequency", 20.0f, 20000.0f, 1000.0f, Unit::Hz),
        linParam("level", "Level", -96.0f, 0.0f, -20.0f, Unit::Db),
    },
    false, true
};

const PluginInfo kDitherInfo = {
    "dither", "Dither", "Dither", Category::Dither,
    {
        choiceParam("bits", "Bit Depth", {"16", "20", "24"}, 0),
        toggleParam("noise_shaping", "Noise Shaping", false),
    },
    false, true
};

const PluginInfo kSynthInfo = {
    "subtractive_synth", "Subtractive Synth", "SubSynth", Category::Instrument,
    {
        choiceParam("osc1_wave", "Osc 1", {"Saw", "Square", "Sine", "Triangle"}, 0),
        choiceParam("osc2_wave", "Osc 2", {"Saw", "Square", "Sine", "Triangle"}, 0),
        linParam("osc2_semitones", "Osc 2 Pitch", -24.0f, 24.0f, 0.0f, Unit::Semitones),
        linParam("osc2_detune", "Osc 2 Detune", -100.0f, 100.0f, 7.0f, Unit::Cents),
        linParam("osc_mix", "Osc Mix", 0.0f, 100.0f, 50.0f, Unit::Percent),
        logParam("cutoff", "Cutoff", 20.0f, 20000.0f, 2000.0f, Unit::Hz),
        linParam("resonance", "Resonance", 0.0f, 100.0f, 20.0f, Unit::Percent),
        linParam("filter_env_amount", "Filter Env", -100.0f, 100.0f, 30.0f, Unit::Percent),
        logParam("amp_attack", "Amp Attack", 0.5f, 5000.0f, 5.0f, Unit::Ms),
        logParam("amp_decay", "Amp Decay", 1.0f, 5000.0f, 200.0f, Unit::Ms),
        linParam("amp_sustain", "Amp Sustain", 0.0f, 100.0f, 70.0f, Unit::Percent),
        logParam("amp_release", "Amp Release", 1.0f, 10000.0f, 300.0f, Unit::Ms),
        logParam("filter_attack", "Filter Attack", 0.5f, 5000.0f, 5.0f, Unit::Ms),
        logParam("filter_decay", "Filter Decay", 1.0f, 5000.0f, 300.0f, Unit::Ms),
        linParam("filter_sustain", "Filter Sustain", 0.0f, 100.0f, 30.0f, Unit::Percent),
        logParam("filter_release", "Filter Release", 1.0f, 10000.0f, 300.0f, Unit::Ms),
        linParam("glide", "Glide", 0.0f, 1000.0f, 0.0f, Unit::Ms),
        linParam("velocity", "Velocity Sens", 0.0f, 100.0f, 70.0f, Unit::Percent),
        linParam("level", "Level", -48.0f, 6.0f, -6.0f, Unit::Db),
    },
    true, false
};

const PluginInfo kDrumInfo = {
    "drum_synth", "Drum Synth", "Drums", Category::Instrument,
    {
        linParam("level", "Level", -48.0f, 6.0f, -6.0f, Unit::Db),
        linParam("kick_tune", "Kick Tune", -12.0f, 12.0f, 0.0f, Unit::Semitones),
        logParam("kick_decay", "Kick Decay", 50.0f, 2000.0f, 450.0f, Unit::Ms),
        linParam("snare_tune", "Snare Tune", -12.0f, 12.0f, 0.0f, Unit::Semitones),
        linParam("snare_snappy", "Snare Snappy", 0.0f, 100.0f, 60.0f, Unit::Percent),
        logParam("snare_decay", "Snare Decay", 50.0f, 1000.0f, 220.0f, Unit::Ms),
        logParam("hat_decay", "Closed Hat Decay", 20.0f, 500.0f, 80.0f, Unit::Ms),
        logParam("open_hat_decay", "Open Hat Decay", 100.0f, 3000.0f, 600.0f, Unit::Ms),
        linParam("tom_tune", "Tom Tune", -12.0f, 12.0f, 0.0f, Unit::Semitones),
        logParam("tom_decay", "Tom Decay", 50.0f, 2000.0f, 500.0f, Unit::Ms),
        logParam("cymbal_decay", "Cymbal Decay", 200.0f, 6000.0f, 2000.0f, Unit::Ms),
    },
    true, false
};

const PluginInfo kSamplerInfo = {
    "sampler", "Sampler", "Sampler", Category::Instrument,
    {
        linParam("root_note", "Root Note", 0.0f, 127.0f, 60.0f, Unit::None),
        linParam("transpose", "Transpose", -48.0f, 48.0f, 0.0f, Unit::Semitones),
        linParam("fine_tune", "Fine Tune", -100.0f, 100.0f, 0.0f, Unit::Cents),
        toggleParam("key_track", "Key Tracking", true),
        linParam("key_low", "Lowest Key", 0.0f, 127.0f, 0.0f, Unit::None),
        linParam("key_high", "Highest Key", 0.0f, 127.0f, 127.0f, Unit::None),
        linParam("vel_low", "Lowest Velocity", 1.0f, 127.0f, 1.0f, Unit::None),
        linParam("vel_high", "Highest Velocity", 1.0f, 127.0f, 127.0f, Unit::None),
        linParam("velocity", "Velocity Sens", 0.0f, 100.0f, 70.0f, Unit::Percent),
        logParam("attack", "Attack", 0.1f, 5000.0f, 1.0f, Unit::Ms),
        logParam("decay", "Decay", 1.0f, 10000.0f, 300.0f, Unit::Ms),
        linParam("sustain", "Sustain", 0.0f, 100.0f, 100.0f, Unit::Percent),
        logParam("release", "Release", 1.0f, 10000.0f, 150.0f, Unit::Ms),
        choiceParam("play_mode", "Play Mode", {"Gate", "One Shot"}, 0),
        linParam("start", "Sample Start", 0.0f, 100.0f, 0.0f, Unit::Percent),
        choiceParam("loop_mode", "Loop", {"Off", "Forward", "Ping-Pong"}, 0),
        linParam("loop_start", "Loop Start", 0.0f, 100.0f, 0.0f, Unit::Percent),
        linParam("loop_end", "Loop End", 0.0f, 100.0f, 100.0f, Unit::Percent),
        linParam("level", "Level", -48.0f, 12.0f, 0.0f, Unit::Db),
    },
    true, false
};

// ============================================================================
// EQ Design Helpers (after SoundCraft crates/dsp/src/plugins/eq.rs)
// ============================================================================

std::array<Coeffs, 2> passFilter(size_t slope, bool high, float freq, float sr) {
    const FilterType k1 = high ? FilterType::HighPass1 : FilterType::LowPass1;
    const FilterType k2 = high ? FilterType::HighPass : FilterType::LowPass;
    switch (slope) {
        case 0:
            return {Coeffs::design(k1, freq, 0.707f, 0.0f, sr), Coeffs::identity()};
        case 2:
            return {Coeffs::design(k1, freq, 0.707f, 0.0f, sr), Coeffs::design(k2, freq, 1.0f, 0.0f, sr)};
        case 3:
            return {Coeffs::design(k2, freq, 0.5411961f, 0.0f, sr), Coeffs::design(k2, freq, 1.306563f, 0.0f, sr)};
        default:
            return {Coeffs::design(k2, freq, kInvSqrt2, 0.0f, sr), Coeffs::identity()};
    }
}

std::array<Coeffs, 9> designEq7(const std::vector<float>& v, float sr) {
    auto val = [&](size_t i) { return i < v.size() ? v[i] : 0.0f; };
    std::array<Coeffs, 9> c;
    c.fill(Coeffs::identity());
    if (val(1) >= 0.5f) {
        auto [a, b] = passFilter(static_cast<size_t>(std::round(val(3))), true, val(2), sr);
        c[0] = a;
        c[1] = b;
    }
    const std::pair<size_t, FilterType> bands[5] = {
        {4, FilterType::LowShelf},
        {7, FilterType::Peak},
        {10, FilterType::Peak},
        {13, FilterType::Peak},
        {16, FilterType::HighShelf},
    };
    for (size_t k = 0; k < 5; ++k) {
        const size_t idx = bands[k].first;
        const float g = val(idx + 1);
        if (std::abs(g) > 1e-4f) {
            c[2 + k] = Coeffs::design(bands[k].second, val(idx), val(idx + 2), g, sr);
        }
    }
    if (val(19) >= 0.5f) {
        auto [a, b] = passFilter(static_cast<size_t>(std::round(val(21))), false, val(20), sr);
        c[7] = a;
        c[8] = b;
    }
    return c;
}

std::array<Coeffs, 1> designEq1(const std::vector<float>& v, float sr) {
    auto val = [&](size_t i) { return i < v.size() ? v[i] : 0.0f; };
    FilterType kind = FilterType::Peak;
    switch (static_cast<int>(std::round(val(0)))) {
        case 1: kind = FilterType::LowShelf; break;
        case 2: kind = FilterType::HighShelf; break;
        case 3: kind = FilterType::HighPass; break;
        case 4: kind = FilterType::LowPass; break;
        case 5: kind = FilterType::Notch; break;
        case 6: kind = FilterType::BandPass; break;
        default: kind = FilterType::Peak; break;
    }
    const float g = val(2);
    if (kind == FilterType::Peak && std::abs(g) < 1e-4f) {
        return {Coeffs::identity()};
    }
    return {Coeffs::design(kind, val(1), val(3), g, sr)};
}

// ============================================================================
// Concrete Plugin Implementations
// ============================================================================

class BasePlugin : public Plugin {
public:
    explicit BasePlugin(const PluginInfo& infoRef) : info_(infoRef) {
        vals_.reserve(info_.params.size());
        for (const auto& p : info_.params) vals_.push_back(p.defaultVal);
    }

    const PluginInfo& info() const override { return info_; }

    bool setParam(const std::string& id, float value) override {
        for (size_t i = 0; i < info_.params.size(); ++i) {
            if (id == info_.params[i].id) {
                vals_[i] = info_.params[i].clamp(value);
                onParamChanged();
                return true;
            }
        }
        return false;
    }

    std::optional<float> param(const std::string& id) const override {
        for (size_t i = 0; i < info_.params.size(); ++i) {
            if (id == info_.params[i].id) return vals_[i];
        }
        return std::nullopt;
    }

protected:
    virtual void onParamChanged() {}
    const PluginInfo& info_;
    std::vector<float> vals_;
    float sr_ = 48000.0f;
    size_t ch_ = 2;
};

// EQ 7-Band and EQ 1-Band
template <size_t N>
class EqPlugin final : public BasePlugin {
public:
    EqPlugin(const PluginInfo& inf, std::array<Coeffs, N> (*designFn)(const std::vector<float>&, float), std::vector<size_t> gainIds)
        : BasePlugin(inf), designFn_(designFn), gainIds_(std::move(gainIds)) {
        onParamChanged();
    }

    void prepare(float sampleRate, size_t, size_t channels) override {
        sr_ = saneSr(sampleRate);
        ch_ = saneChannels(channels);
        filt_.assign(ch_, {});
        onParamChanged();
    }

    void reset() override {
        for (auto& chF : filt_) {
            for (auto& b : chF) b.reset();
        }
    }

    void process(std::vector<std::vector<float>>& io, size_t frames) override {
        const size_t cMax = std::min(ch_, io.size());
        for (size_t c = 0; c < cMax; ++c) {
            const size_t n = std::min(frames, io[c].size());
            for (size_t i = 0; i < n; ++i) {
                float y = io[c][i];
                for (size_t s = 0; s < N; ++s) {
                    y = filt_[c][s].processSample(y);
                }
                io[c][i] = y * gainLin_;
            }
        }
    }

protected:
    void onParamChanged() override {
        coeffs_ = designFn_(vals_, sr_);
        float totalDb = 0.0f;
        for (size_t idx : gainIds_) {
            if (idx < vals_.size()) totalDb += vals_[idx];
        }
        gainLin_ = dbToGain(totalDb);
        for (auto& chF : filt_) {
            for (size_t s = 0; s < N; ++s) chF[s].coeffs = coeffs_[s];
        }
    }

private:
    std::array<Coeffs, N> (*designFn_)(const std::vector<float>&, float);
    std::vector<size_t> gainIds_;
    std::array<Coeffs, N> coeffs_{};
    std::vector<std::array<Biquad, N>> filt_;
    float gainLin_ = 1.0f;
};

// Compressor / Limiter
class CompressorPlugin final : public BasePlugin {
public:
    CompressorPlugin() : BasePlugin(kCompInfo) {}

    void prepare(float sampleRate, size_t, size_t channels) override {
        sr_ = saneSr(sampleRate);
        ch_ = saneChannels(channels);
        sc_.assign(ch_, Biquad{});
        gr_ = 0.0f;
        grMeter_ = 0.0f;
        onParamChanged();
    }

    void reset() override {
        for (auto& b : sc_) b.reset();
        gr_ = 0.0f;
        grMeter_ = 0.0f;
    }

    void process(std::vector<std::vector<float>>& io, size_t frames) override {
        const size_t cMax = std::min(ch_, io.size());
        const bool hpfOn = vals_[6] >= 0.5f;
        const float ratio = (vals_[8] >= 0.5f) ? 1000.0f : vals_[1];
        const float thresh = vals_[0];
        const float knee = vals_[4];
        const float makeup = dbToGain(vals_[5]);
        const float mix = vals_[10] / 100.0f;
        float maxGr = 0.0f;

        for (size_t n = 0; n < frames; ++n) {
            float level = 0.0f;
            for (size_t c = 0; c < cMax; ++c) {
                if (n >= io[c].size()) continue;
                const float x = io[c][n];
                const float s = hpfOn ? sc_[c].processSample(x) : x;
                if (std::isfinite(s)) level = std::max(level, std::abs(s));
            }
            const float target = compressGr(gainToDb(level), thresh, ratio, knee);
            const float coef = (target > gr_) ? att_ : rel_;
            gr_ = flushDenorm(target + (gr_ - target) * coef);
            maxGr = std::max(maxGr, gr_);
            const float g = dbToGain(-gr_) * makeup;
            for (size_t c = 0; c < cMax; ++c) {
                if (n >= io[c].size()) continue;
                const float dry = io[c][n];
                io[c][n] = dry * (1.0f - mix) + dry * g * mix;
            }
        }
        grMeter_ = maxGr;
    }

    float gainReductionDb() const override { return grMeter_; }

protected:
    void onParamChanged() override {
        att_ = timeCoef(vals_[2], sr_);
        rel_ = timeCoef(vals_[3], sr_);
        for (auto& b : sc_) b.set(FilterType::HighPass, vals_[7], kInvSqrt2, 0.0f, sr_);
    }

private:
    std::vector<Biquad> sc_;
    float gr_ = 0.0f;
    float grMeter_ = 0.0f;
    float att_ = 0.0f;
    float rel_ = 0.0f;
};

// Maximizer
class MaximizerPlugin final : public BasePlugin {
public:
    MaximizerPlugin() : BasePlugin(kMaxInfo) {}

    void prepare(float sampleRate, size_t, size_t channels) override {
        sr_ = saneSr(sampleRate);
        ch_ = saneChannels(channels);
        grMeter_ = 0.0f;
    }

    void reset() override { grMeter_ = 0.0f; }

    void process(std::vector<std::vector<float>>& io, size_t frames) override {
        const size_t cMax = std::min(ch_, io.size());
        const float drive = dbToGain(-vals_[0]);
        const float ceilLin = dbToGain(vals_[1]);
        float minGain = 1.0f;
        for (size_t n = 0; n < frames; ++n) {
            float peak = 0.0f;
            for (size_t c = 0; c < cMax; ++c) {
                if (n < io[c].size() && std::isfinite(io[c][n])) {
                    peak = std::max(peak, std::abs(io[c][n] * drive));
                }
            }
            const float g = (peak > ceilLin && peak > 0.0f) ? (ceilLin / peak) : 1.0f;
            minGain = std::min(minGain, g);
            for (size_t c = 0; c < cMax; ++c) {
                if (n < io[c].size()) {
                    io[c][n] = std::clamp(io[c][n] * drive * g, -ceilLin, ceilLin);
                }
            }
        }
        grMeter_ = -gainToDb(minGain);
    }

    float gainReductionDb() const override { return grMeter_; }

private:
    float grMeter_ = 0.0f;
};

// Reverb (Room / Plate)
class ReverbPlugin final : public BasePlugin {
public:
    explicit ReverbPlugin(const PluginInfo& inf) : BasePlugin(inf) {}

    void prepare(float sampleRate, size_t, size_t channels) override {
        sr_ = saneSr(sampleRate);
        ch_ = saneChannels(channels);
        static const float kDelaysMs[4] = {23.1f, 31.7f, 41.3f, 53.9f};
        for (int i = 0; i < 4; ++i) {
            lines_[i].allocate(static_cast<size_t>(msToSamples(kDelaysMs[i] * 2.0f, sr_)) + 16);
            lp_[i] = 0.0f;
        }
        onParamChanged();
    }

    void reset() override {
        for (int i = 0; i < 4; ++i) {
            lines_[i].clear();
            lp_[i] = 0.0f;
        }
    }

    void process(std::vector<std::vector<float>>& io, size_t frames) override {
        const size_t cMax = std::min(ch_, io.size());
        const float mix = vals_[0] / 100.0f;
        const float sizeFactor = 0.5f + (vals_[3] / 100.0f);
        static const float kDelaysMs[4] = {23.1f, 31.7f, 41.3f, 53.9f};
        size_t taps[4];
        for (int i = 0; i < 4; ++i) {
            taps[i] = std::max<size_t>(1, static_cast<size_t>(msToSamples(kDelaysMs[i] * sizeFactor, sr_)));
        }

        for (size_t n = 0; n < frames; ++n) {
            float inMono = 0.0f;
            for (size_t c = 0; c < cMax; ++c) {
                if (n < io[c].size() && std::isfinite(io[c][n])) inMono += io[c][n];
            }
            inMono /= static_cast<float>(std::max<size_t>(1, cMax));

            float outs[4];
            for (int i = 0; i < 4; ++i) {
                const float d = lines_[i].tap(taps[i]);
                lp_[i] = flushDenorm(d + (lp_[i] - d) * damp_);
                outs[i] = lp_[i];
            }
            const float sum = 0.5f * (outs[0] + outs[1] + outs[2] + outs[3]);
            for (int i = 0; i < 4; ++i) {
                lines_[i].push(inMono * 0.25f + (sum - outs[i]) * fb_);
            }
            const float wetL = (outs[0] + outs[2]) * 0.5f;
            const float wetR = (outs[1] + outs[3]) * 0.5f;
            for (size_t c = 0; c < cMax; ++c) {
                if (n >= io[c].size()) continue;
                const float wet = (c % 2 == 0) ? wetL : wetR;
                io[c][n] = io[c][n] * (1.0f - mix) + wet * mix;
            }
        }
    }

protected:
    void onParamChanged() override {
        const float rt60 = std::max(0.1f, vals_[2]);
        fb_ = std::clamp(std::pow(10.0f, -3.0f * 0.035f / rt60), 0.1f, 0.98f);
        damp_ = std::exp(-kTauF * vals_[4] / std::max(1.0f, sr_));
    }

private:
    DelayLine lines_[4];
    float lp_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float fb_ = 0.75f;
    float damp_ = 0.2f;
};

// Mod Delay / Chorus / Flanger
class DelayModPlugin final : public BasePlugin {
public:
    explicit DelayModPlugin(const PluginInfo& inf, int mode) : BasePlugin(inf), mode_(mode) {}

    void prepare(float sampleRate, size_t, size_t channels) override {
        sr_ = saneSr(sampleRate);
        ch_ = saneChannels(channels);
        lines_.assign(ch_, DelayLine{});
        for (auto& d : lines_) d.allocate(static_cast<size_t>(msToSamples(2100.0f, sr_)) + 16);
        phase_ = 0.0f;
    }

    void reset() override {
        for (auto& d : lines_) d.clear();
        phase_ = 0.0f;
    }

    void process(std::vector<std::vector<float>>& io, size_t frames) override {
        const size_t cMax = std::min(ch_, io.size());
        const float rateHz = (mode_ == 0) ? vals_[3] : vals_[0];
        const float depthPct = (mode_ == 0) ? vals_[4] : vals_[1];
        const float baseDelayMs = (mode_ == 0) ? vals_[0] : vals_[2];
        const float fb = ((mode_ == 0) ? vals_[1] : vals_[3]) / 100.0f;
        const float mix = ((mode_ == 0) ? vals_[2] : vals_[4]) / 100.0f;
        const float maxSwingMs = (mode_ == 1) ? 6.0f : 3.5f;

        for (size_t n = 0; n < frames; ++n) {
            phase_ += rateHz / sr_;
            if (phase_ >= 1.0f) phase_ -= 1.0f;
            for (size_t c = 0; c < cMax; ++c) {
                if (n >= io[c].size()) continue;
                const float chPh = phase_ + ((c % 2 == 1) ? 0.25f : 0.0f);
                const float lfo = 0.5f * (1.0f + std::sin(kTauF * chPh));
                const float dSamples = msToSamples(baseDelayMs + (depthPct / 100.0f) * maxSwingMs * lfo, sr_);
                const float dry = std::isfinite(io[c][n]) ? io[c][n] : 0.0f;
                const float delayed = lines_[c].readLinear(dSamples);
                lines_[c].push(dry + fb * delayed);
                io[c][n] = dry * (1.0f - mix) + delayed * mix;
            }
        }
    }

private:
    int mode_ = 0; // 0 = ModDelay, 1 = Chorus, 2 = Flanger
    std::vector<DelayLine> lines_;
    float phase_ = 0.0f;
};

// Saturator
class SaturatorPlugin final : public BasePlugin {
public:
    SaturatorPlugin() : BasePlugin(kSatInfo) {}

    void prepare(float sampleRate, size_t, size_t channels) override {
        sr_ = saneSr(sampleRate);
        ch_ = saneChannels(channels);
        dc_.assign(ch_, DcBlocker{});
        for (auto& d : dc_) d.setCutoff(10.0f, sr_);
    }

    void reset() override {
        for (auto& d : dc_) d.reset();
    }

    void process(std::vector<std::vector<float>>& io, size_t frames) override {
        const size_t cMax = std::min(ch_, io.size());
        const float g = dbToGain(vals_[0]);
        const float b = vals_[1] / 100.0f * 0.5f;
        const float mix = vals_[3] / 100.0f;
        const float outG = dbToGain(vals_[4]);
        const float tb = std::tanh(b);
        const float norm = 1.0f / std::max(1e-3f, std::tanh(g + b) - tb);

        for (size_t c = 0; c < cMax; ++c) {
            const size_t n = std::min(frames, io[c].size());
            for (size_t i = 0; i < n; ++i) {
                const float dry = std::isfinite(io[c][i]) ? io[c][i] : 0.0f;
                float y = (std::tanh(g * dry + b) - tb) * norm;
                if (b > 0.0f) y = dc_[c].process(y);
                io[c][i] = (dry * (1.0f - mix) + y * mix) * outG;
            }
        }
    }

private:
    std::vector<DcBlocker> dc_;
};

// Gain / Trim / Invert / Generic Pass-Through
class SimpleUtilityPlugin final : public BasePlugin {
public:
    explicit SimpleUtilityPlugin(const PluginInfo& inf) : BasePlugin(inf) {}

    void prepare(float sampleRate, size_t, size_t channels) override {
        sr_ = saneSr(sampleRate);
        ch_ = saneChannels(channels);
    }

    void reset() override {}

    void process(std::vector<std::vector<float>>& io, size_t frames) override {
        const size_t cMax = std::min(ch_, io.size());
        const std::string id = info_.id;
        float gain = 1.0f;
        if (id == "gain") {
            gain = vals_[0] <= -95.9f ? 0.0f : dbToGain(vals_[0]);
        } else if (id == "trim") {
            gain = (vals_[0] <= -95.9f ? 0.0f : dbToGain(vals_[0])) * (vals_[1] >= 0.5f ? -1.0f : 1.0f);
        } else if (id == "invert") {
            gain = -1.0f;
        }
        if (gain == 1.0f) return;
        for (size_t c = 0; c < cMax; ++c) {
            const size_t n = std::min(frames, io[c].size());
            for (size_t i = 0; i < n; ++i) {
                io[c][i] *= gain;
            }
        }
    }
};

} // namespace

// after SoundCraft (crates/dsp/src/plugins/eq.rs eq7_response)
std::vector<float> eq7Response(const std::vector<std::pair<std::string, float>>& params,
                               const std::vector<float>& freqs, float sr) {
    std::vector<float> vals;
    vals.reserve(kEq7Info.params.size());
    for (const auto& p : kEq7Info.params) vals.push_back(p.defaultVal);
    for (const auto& [k, v] : params) {
        for (size_t i = 0; i < kEq7Info.params.size(); ++i) {
            if (k == kEq7Info.params[i].id) {
                vals[i] = kEq7Info.params[i].clamp(v);
                break;
            }
        }
    }
    const auto coeffs = designEq7(vals, sr);
    const float g = vals[0] + vals[22];
    std::vector<float> out;
    out.reserve(freqs.size());
    for (float f : freqs) {
        float sum = g;
        for (const auto& c : coeffs) sum += c.magnitudeDb(f, sr);
        out.push_back(sum);
    }
    return out;
}

// after SoundCraft (crates/dsp/src/plugins/eq.rs eq1_response)
std::vector<float> eq1Response(const std::vector<std::pair<std::string, float>>& params,
                               const std::vector<float>& freqs, float sr) {
    std::vector<float> vals;
    vals.reserve(kEq1Info.params.size());
    for (const auto& p : kEq1Info.params) vals.push_back(p.defaultVal);
    for (const auto& [k, v] : params) {
        for (size_t i = 0; i < kEq1Info.params.size(); ++i) {
            if (k == kEq1Info.params[i].id) {
                vals[i] = kEq1Info.params[i].clamp(v);
                break;
            }
        }
    }
    const auto coeffs = designEq1(vals, sr);
    const float g = vals[4];
    std::vector<float> out;
    out.reserve(freqs.size());
    for (float f : freqs) {
        out.push_back(g + coeffs[0].magnitudeDb(f, sr));
    }
    return out;
}

// after SoundCraft (crates/dsp/src/plugins.rs REGISTRY)
const std::vector<const PluginInfo*>& plugins() {
    static const std::vector<const PluginInfo*> kRegistry = {
        &kEq1Info,
        &kEq7Info,
        &kCompInfo,
        &kGateInfo,
        &kDeEssInfo,
        &kMaxInfo,
        &kStripInfo,
        &kRoomInfo,
        &kPlateInfo,
        &kDelayInfo,
        &kAutoPanInfo,
        &kChorusInfo,
        &kFlangerInfo,
        &kPhaserInfo,
        &kRingInfo,
        &kSatInfo,
        &kLoFiInfo,
        &kRectInfo,
        &kSubInfo,
        &kPitchInfo,
        &kShiftInfo,
        &kGainInfo,
        &kTrimInfo,
        &kInvertInfo,
        &kDcInfo,
        &kGenInfo,
        &kDitherInfo,
        &kSynthInfo,
        &kDrumInfo,
        &kSamplerInfo,
    };
    return kRegistry;
}

const PluginInfo* pluginInfo(const std::string& id) {
    for (const PluginInfo* info : plugins()) {
        if (id == info->id) return info;
    }
    return nullptr;
}

std::unique_ptr<Plugin> create(const std::string& id) {
    const PluginInfo* inf = pluginInfo(id);
    if (!inf) return nullptr;
    std::unique_ptr<Plugin> p;
    if (id == "eq_7band") {
        p = std::make_unique<EqPlugin<9>>(kEq7Info, &designEq7, std::vector<size_t>{0, 22});
    } else if (id == "eq_1band") {
        p = std::make_unique<EqPlugin<1>>(kEq1Info, &designEq1, std::vector<size_t>{4});
    } else if (id == "compressor") {
        p = std::make_unique<CompressorPlugin>();
    } else if (id == "maximizer") {
        p = std::make_unique<MaximizerPlugin>();
    } else if (id == "room_reverb") {
        p = std::make_unique<ReverbPlugin>(kRoomInfo);
    } else if (id == "plate_reverb") {
        p = std::make_unique<ReverbPlugin>(kPlateInfo);
    } else if (id == "mod_delay") {
        p = std::make_unique<DelayModPlugin>(kDelayInfo, 0);
    } else if (id == "chorus") {
        p = std::make_unique<DelayModPlugin>(kChorusInfo, 1);
    } else if (id == "flanger") {
        p = std::make_unique<DelayModPlugin>(kFlangerInfo, 2);
    } else if (id == "saturator") {
        p = std::make_unique<SaturatorPlugin>();
    } else {
        p = std::make_unique<SimpleUtilityPlugin>(*inf);
    }
    p->prepare(48000.0f, 1024, 2);
    return p;
}

bool isGenerator(const std::string& id) {
    return id == "signal_generator";
}

} // namespace Dsp
