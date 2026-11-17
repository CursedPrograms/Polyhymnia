// Tempo and meter map (after SoundCraft crates/time/src/tempo.rs).
#pragma once
#include "time_samples.h"

#include <cstdint>
#include <vector>

namespace Time {

struct TempoEvent {
    int64_t tick = 0;
    double bpm = 120.0;
};

struct MeterEvent {
    int64_t tick = 0;
    uint32_t numerator = 4;
    uint32_t denominator = 4;
    int64_t ticksPerBeat() const;
    int64_t ticksPerBar() const;
};

struct BarBeat {
    int64_t bar = 1;
    int64_t beat = 1;
    int64_t tick = 0;
};

class TempoMap {
public:
    static TempoMap defaultMap();
    static TempoMap make(double bpm, uint32_t num, uint32_t den);

    bool setTempo(int64_t tick, double bpm);
    bool setMeter(int64_t tick, uint32_t num, uint32_t den);

    const std::vector<TempoEvent>& tempos() const { return tempos_; }
    const std::vector<MeterEvent>& meters() const { return meters_; }

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

private:
    std::vector<TempoEvent> tempos_;
    std::vector<MeterEvent> meters_;
    std::vector<double> elapsed_;

    void reindex();
    void segment(size_t i, int64_t& tick, double& bpm, double& elapsed) const;
    static double samplesPerTick(double bpm, SampleRate sr);
    static double samplesPerElapsed(SampleRate sr);

    void putMeter(const MeterEvent& ev);
    std::pair<int64_t, int64_t> lastSegment() const;
    void restoreMetersAtBars(const std::vector<std::pair<int64_t, MeterEvent>>& later);
    std::vector<std::pair<int64_t, MeterEvent>> takeMetersAfter(int64_t tick);
};

} // namespace Time