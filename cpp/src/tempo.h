// Polyhymnia - tempo and meter (after SoundCraft crates/time); tempo.h avoids clashing with the C time header.
#pragma once
#include "tempo_map.h"

#include <vector>

namespace Time {

struct Meter {
    int num = 4;
    int den = 4;
};

struct Map {
    double tempoBpm = 120.0;
    Meter meter;
    std::vector<TempoEvent> tempos;
    std::vector<MeterEvent> meters;

    // Samples per quarter at the given sample rate with the current tempo.
    double samplesPerBeat(double sampleRate) const;
    double barsAtSample(long long sample, double sampleRate) const;

    TempoMap toTempoMap() const;
    void setFromTempoMap(const TempoMap& m);
};

} // namespace Time
