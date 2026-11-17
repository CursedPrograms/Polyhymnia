// Polyhymnia - tempo helpers: see tempo.h.
#include "tempo.h"

namespace Time {

double Map::samplesPerBeat(double sampleRate) const {
    if (tempoBpm <= 0.0) return sampleRate;
    return sampleRate * 60.0 / tempoBpm;
}

double Map::barsAtSample(long long sample, double sampleRate) const {
    const double beat = samplesPerBeat(sampleRate);
    if (beat <= 0.0) return 0.0;
    const double beats = sample / beat;
    const double beatsPerBar = meter.num > 0 ? (double)meter.num : 4.0;
    return beats / beatsPerBar;
}

}