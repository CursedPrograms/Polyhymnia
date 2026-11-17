// Tempo and meter map (after SoundCraft crates/time/src/tempo.rs).
#pragma once
#include "time_samples.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace Time {

struct TempoEvent {
    int64_t tick = 0;
    double bpm = 120.0;
    bool operator==(const TempoEvent& o) const { return tick == o.tick && bpm == o.bpm; }
    bool operator!=(const TempoEvent& o) const { return !(*this == o); }
};

struct MeterEvent {
    int64_t tick = 0;
    uint32_t numerator = 4;
    uint32_t denominator = 4;
    int64_t ticksPerBeat() const;
    int64_t ticksPerBar() const;
    bool operator==(const MeterEvent& o) const { return tick == o.tick && numerator == o.numerator && denominator == o.denominator; }
    bool operator!=(const MeterEvent& o) const { return !(*this == o); }
};

struct BarBeat {
    int64_t bar = 1;
    int64_t beat = 1;
    int64_t tick = 0;
    bool operator==(const BarBeat& o) const { return bar == o.bar && beat == o.beat && tick == o.tick; }
    bool operator!=(const BarBeat& o) const { return !(*this == o); }
};

bool validBpm(double bpm);
bool validMeter(uint32_t numerator, uint32_t denominator);

class TempoMap {
public:
    TempoMap();

    static TempoMap defaultMap();
    static std::optional<TempoMap> tryNew(double bpm, uint32_t num, uint32_t den);
    static TempoMap make(double bpm, uint32_t num, uint32_t den);
    static TempoMap fromParts(std::vector<TempoEvent> tempos, std::vector<MeterEvent> meters);

    void sanitize();

    bool setTempo(int64_t tick, double bpm);
    bool removeTempo(int64_t tick);
    bool setMeter(int64_t tick, uint32_t num, uint32_t den);
    bool removeMeter(int64_t tick);
    bool scaleTempos(double factor);

    const std::vector<TempoEvent>& tempos() const { return tempos_; }
    const std::vector<MeterEvent>& meters() const { return meters_; }
    std::vector<TempoEvent>& temposMut() { return tempos_; }
    std::vector<MeterEvent>& metersMut() { return meters_; }
    size_t elapsedLen() const { return elapsed_.size(); }

    double tempoAtTick(int64_t tick) const;
    MeterEvent meterAtTick(int64_t tick) const;
    Samples tickToSamples(int64_t tick, SampleRate sr) const;
    double tickToSamplesF(int64_t tick, SampleRate sr) const;
    double samplesToTicksF(Samples s, SampleRate sr) const;
    int64_t samplesToTicks(Samples s, SampleRate sr) const;
    int64_t barStartTick(int64_t bar) const;
    BarBeat barBeatAtTick(int64_t tick) const;
    BarBeat barBeatAt(Samples s, SampleRate sr) const { return barBeatAtTick(samplesToTicks(s, sr)); }

    // after SoundCraft (crates/time/src/tempo.rs tick_at_bar_beat)
    int64_t tickAtBarBeat(const BarBeat& bb) const;

    // after SoundCraft (crates/time/src/tempo.rs samples_at_bar_beat)
    Samples samplesAtBarBeat(const BarBeat& bb, SampleRate sr) const;

    static double samplesPerTick(double bpm, SampleRate sr);

    bool operator==(const TempoMap& o) const { return tempos_ == o.tempos_ && meters_ == o.meters_; }
    bool operator!=(const TempoMap& o) const { return !(*this == o); }

private:
    std::vector<TempoEvent> tempos_;
    std::vector<MeterEvent> meters_;
    std::vector<double> elapsed_;

    void reindex();
    void segment(size_t i, int64_t& tick, double& bpm, double& elapsed) const;
    static double samplesPerElapsed(SampleRate sr);

    void putMeter(const MeterEvent& ev);
    std::pair<int64_t, int64_t> lastSegment() const;
    void restoreMetersAtBars(const std::vector<std::pair<int64_t, MeterEvent>>& later);
    std::vector<std::pair<int64_t, MeterEvent>> takeMetersAfter(int64_t tick);
};

} // namespace Time
