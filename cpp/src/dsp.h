// Polyhymnia - Built-in DSP effects, biquad filters, and plugin registry (after SoundCraft crates/dsp/src/*).
#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Dsp {

constexpr float kMinDb = -144.0f;

float dbToGain(float db);
float gainToDb(float g);
double midiToHz(double note);
float compressGr(float levelDb, float threshold, float ratio, float knee);

// after SoundCraft (crates/dsp/src/lib.rs Category)
enum class Category {
    Eq,
    Dynamics,
    PitchShift,
    Reverb,
    Delay,
    Modulation,
    Harmonic,
    Dither,
    Instrument,
    Other,
};

const char* categoryLabel(Category c);

// after SoundCraft (crates/dsp/src/lib.rs Unit)
enum class Unit {
    None,
    Db,
    Hz,
    Ms,
    Percent,
    Ratio,
    Semitones,
    Cents,
    Seconds,
    Toggle,
    Choice,
};

// after SoundCraft (crates/dsp/src/lib.rs Taper)
enum class Taper {
    Linear,
    Log,
};

// after SoundCraft (crates/dsp/src/lib.rs ParamInfo)
struct ParamInfo {
    const char* id = "";
    const char* name = "";
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float defaultVal = 0.0f;
    Unit unit = Unit::None;
    Taper taper = Taper::Linear;
    std::vector<const char*> choices;

    float clamp(float value) const;
    float toNormalized(float value) const;
    float fromNormalized(float t) const;
    std::string format(float value) const;
};

// after SoundCraft (crates/dsp/src/lib.rs PluginInfo)
struct PluginInfo {
    const char* id = "";
    const char* name = "";
    const char* shortName = "";
    Category category = Category::Other;
    std::vector<ParamInfo> params;
    bool isInstrument = false;
    bool audiosuite = true;

    const ParamInfo* param(const std::string& paramId) const;
};

// after SoundCraft (crates/dsp/src/biquad.rs FilterType, Coeffs, Biquad)
enum class FilterType {
    LowPass,
    HighPass,
    BandPass,
    Notch,
    Peak,
    LowShelf,
    HighShelf,
    AllPass,
    LowPass1,
    HighPass1,
};

struct Coeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    static Coeffs identity() { return {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}; }
    static Coeffs design(FilterType kind, float freq, float q, float gainDb, float sampleRate);
    float magnitudeDb(float freq, float sampleRate) const;
};

struct Biquad {
    Coeffs coeffs;
    float z1 = 0.0f;
    float z2 = 0.0f;

    static Biquad design(FilterType kind, float freq, float q, float gainDb, float sampleRate);
    void set(FilterType kind, float freq, float q, float gainDb, float sampleRate);
    void reset();
    float processSample(float x);
    void processBlock(float* buf, size_t frames);
    float magnitudeDb(float freq, float sampleRate) const;
};

// after SoundCraft (crates/dsp/src/plugins/eq.rs eq7_response, eq1_response)
std::vector<float> eq7Response(const std::vector<std::pair<std::string, float>>& params,
                               const std::vector<float>& freqs, float sr);
std::vector<float> eq1Response(const std::vector<std::pair<std::string, float>>& params,
                               const std::vector<float>& freqs, float sr);

// after SoundCraft (crates/dsp/src/lib.rs Plugin)
class Plugin {
public:
    virtual ~Plugin() = default;
    virtual const PluginInfo& info() const = 0;
    virtual void prepare(float sampleRate, size_t maxBlock, size_t channels) = 0;
    virtual void reset() = 0;
    virtual bool setParam(const std::string& id, float value) = 0;
    virtual std::optional<float> param(const std::string& id) const = 0;
    virtual void process(std::vector<std::vector<float>>& io, size_t frames) = 0;
    virtual size_t latency() const { return 0; }
    virtual size_t tailSamples() const { return 0; }
    virtual float gainReductionDb() const { return 0.0f; }
};

const std::vector<const PluginInfo*>& plugins();
const PluginInfo* pluginInfo(const std::string& id);
std::unique_ptr<Plugin> create(const std::string& id);
bool isGenerator(const std::string& id);

} // namespace Dsp
