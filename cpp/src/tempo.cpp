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

TempoMap Map::toTempoMap() const {
    std::vector<TempoEvent> t = tempos;
    if (t.empty()) {
        t.push_back({0, tempoBpm});
    } else if (t.front().tick == 0) {
        t.front().bpm = tempoBpm;
    }
    std::vector<MeterEvent> m = meters;
    if (m.empty()) {
        m.push_back({0, static_cast<uint32_t>(meter.num > 0 ? meter.num : 4), static_cast<uint32_t>(meter.den > 0 ? meter.den : 4)});
    } else if (m.front().tick == 0) {
        m.front().numerator = static_cast<uint32_t>(meter.num > 0 ? meter.num : 4);
        m.front().denominator = static_cast<uint32_t>(meter.den > 0 ? meter.den : 4);
    }
    TempoMap out = TempoMap::fromParts(std::move(t), std::move(m));
    out.sanitize();
    return out;
}

void Map::setFromTempoMap(const TempoMap& m) {
    tempos = m.tempos();
    meters = m.meters();
    if (!tempos.empty()) tempoBpm = tempos.front().bpm;
    if (!meters.empty()) {
        meter.num = static_cast<int>(meters.front().numerator);
        meter.den = static_cast<int>(meters.front().denominator);
    }
}

} // namespace Time
