// Polyhymnia - tempo and meter (after SoundCraft crates/time); tempo.h avoids clashing with the C time header.
#pragma once

namespace Time {
struct Meter {
    int num = 4;
    int den = 4;
};
struct Map {
    double tempoBpm = 120.0;
    Meter meter;
    // Samples per quarter at 48 kHz with the current tempo.
    double samplesPerBeat(double sampleRate) const;
    double barsAtSample(long long sample, double sampleRate) const;
};
}