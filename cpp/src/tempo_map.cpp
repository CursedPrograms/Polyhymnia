// Tempo map tick/sample conversion (after SoundCraft crates/time/src/tempo.rs).
#include "tempo_map.h"
#include "time_range.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace Time {

// after SoundCraft (crates/time/src/tempo.rs ceil_div)
static int64_t ceilDiv(int64_t num, int64_t den) {
    if (den <= 0) return 0;
    return num / den + static_cast<int64_t>(num % den != 0);
}

// after SoundCraft (crates/time/src/tempo.rs valid_bpm)
bool validBpm(double bpm) { return std::isfinite(bpm) && bpm >= 5.0 && bpm <= 1000.0; }

// after SoundCraft (crates/time/src/tempo.rs valid_meter)
bool validMeter(uint32_t numerator, uint32_t denominator) {
    if (numerator < 1 || numerator > 64) return false;
    static const uint32_t validDen[] = {1, 2, 4, 8, 16, 32, 64};
    return std::find(std::begin(validDen), std::end(validDen), denominator) != std::end(validDen);
}

// after SoundCraft (crates/time/src/tempo.rs MeterEvent::ticks_per_beat)
int64_t MeterEvent::ticksPerBeat() const {
    const int64_t d = std::max<int64_t>(1, static_cast<int64_t>(denominator));
    return std::max<int64_t>(1, TICKS_PER_QUARTER * 4 / d);
}

// after SoundCraft (crates/time/src/tempo.rs MeterEvent::ticks_per_bar)
int64_t MeterEvent::ticksPerBar() const {
    return Time::satMul(ticksPerBeat(), std::max<int64_t>(1, static_cast<int64_t>(numerator)));
}

TempoMap::TempoMap() {
    tempos_.push_back({0, 120.0});
    meters_.push_back({0, 4, 4});
    reindex();
}

TempoMap TempoMap::defaultMap() { return TempoMap(); }

// after SoundCraft (crates/time/src/tempo.rs TempoMap::from_parts)
TempoMap TempoMap::fromParts(std::vector<TempoEvent> tempos, std::vector<MeterEvent> meters) {
    TempoMap m;
    m.tempos_ = std::move(tempos);
    m.meters_ = std::move(meters);
    m.reindex();
    return m;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::new)
std::optional<TempoMap> TempoMap::tryNew(double bpm, uint32_t num, uint32_t den) {
    TempoMap m = defaultMap();
    if (!m.setTempo(0, bpm)) return std::nullopt;
    if (!m.setMeter(0, num, den)) return std::nullopt;
    return m;
}

TempoMap TempoMap::make(double bpm, uint32_t num, uint32_t den) {
    TempoMap m = defaultMap();
    m.setTempo(0, bpm);
    m.setMeter(0, num, den);
    return m;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::reindex)
void TempoMap::reindex() {
    elapsed_.clear();
    elapsed_.reserve(tempos_.size());
    double acc = 0.0;
    int64_t segTick = 0;
    double bpm = tempos_.empty() ? 120.0 : tempos_.front().bpm;
    for (size_t i = 0; i < tempos_.size(); ++i) {
        const auto& e = tempos_[i];
        if (i > 0) {
            acc += static_cast<double>(Time::satSub(e.tick, segTick)) / bpm;
            segTick = e.tick;
            bpm = e.bpm;
        }
        elapsed_.push_back(acc);
    }
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::sanitize)
void TempoMap::sanitize() {
    tempos_.erase(std::remove_if(tempos_.begin(), tempos_.end(),
                                 [](const TempoEvent& e) { return e.tick < 0 || e.tick > MAX_POSITION || !validBpm(e.bpm); }),
                  tempos_.end());
    std::stable_sort(tempos_.begin(), tempos_.end(), [](const TempoEvent& a, const TempoEvent& b) { return a.tick < b.tick; });
    tempos_.erase(std::unique(tempos_.begin(), tempos_.end(), [](const TempoEvent& a, const TempoEvent& b) { return a.tick == b.tick; }),
                  tempos_.end());
    if (tempos_.empty() || tempos_.front().tick > 0) {
        const double bpm = tempos_.empty() ? 120.0 : tempos_.front().bpm;
        tempos_.insert(tempos_.begin(), {0, bpm});
    }

    meters_.erase(std::remove_if(meters_.begin(), meters_.end(),
                                 [](const MeterEvent& m) { return m.tick < 0 || m.tick > MAX_POSITION || !validMeter(m.numerator, m.denominator); }),
                  meters_.end());
    std::stable_sort(meters_.begin(), meters_.end(), [](const MeterEvent& a, const MeterEvent& b) { return a.tick < b.tick; });
    meters_.erase(std::unique(meters_.begin(), meters_.end(), [](const MeterEvent& a, const MeterEvent& b) { return a.tick == b.tick; }),
                  meters_.end());
    if (meters_.empty() || meters_.front().tick > 0) {
        const uint32_t num = meters_.empty() ? 4 : meters_.front().numerator;
        const uint32_t den = meters_.empty() ? 4 : meters_.front().denominator;
        meters_.insert(meters_.begin(), {0, num, den});
    }
    reindex();
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::set_tempo)
bool TempoMap::setTempo(int64_t tick, double bpm) {
    if (!validBpm(bpm)) return false;
    tick = std::max<int64_t>(0, tick);
    const auto it = std::lower_bound(tempos_.begin(), tempos_.end(), tick, [](const TempoEvent& e, int64_t t) { return e.tick < t; });
    if (it != tempos_.end() && it->tick == tick)
        it->bpm = bpm;
    else
        tempos_.insert(it, {tick, bpm});
    reindex();
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::remove_tempo)
bool TempoMap::removeTempo(int64_t tick) {
    if (tick == 0) return false;
    const size_t before = tempos_.size();
    tempos_.erase(std::remove_if(tempos_.begin(), tempos_.end(), [&](const TempoEvent& e) { return e.tick == tick; }), tempos_.end());
    reindex();
    return tempos_.size() != before;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::put_meter)
void TempoMap::putMeter(const MeterEvent& ev) {
    const auto it = std::lower_bound(meters_.begin(), meters_.end(), ev.tick, [](const MeterEvent& e, int64_t t) { return e.tick < t; });
    if (it != meters_.end() && it->tick == ev.tick)
        *it = ev;
    else
        meters_.insert(it, ev);
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::take_meters_after)
std::vector<std::pair<int64_t, MeterEvent>> TempoMap::takeMetersAfter(int64_t tick) {
    std::vector<std::pair<int64_t, MeterEvent>> later;
    int64_t bar = 1, segTick = 0;
    for (size_t i = 0; i < meters_.size(); ++i) {
        const auto& m = meters_[i];
        const int64_t tpb = m.ticksPerBar();
        if (m.tick > tick) later.push_back({Time::satAdd(bar, Time::satSub(m.tick, segTick) / tpb), m});
        if (i + 1 < meters_.size()) {
            const int64_t nt = meters_[i + 1].tick;
            bar = Time::satAdd(bar, ceilDiv(Time::satSub(nt, segTick), tpb));
            segTick = nt;
        }
    }
    meters_.erase(std::remove_if(meters_.begin(), meters_.end(), [&](const MeterEvent& e) { return e.tick > tick; }), meters_.end());
    return later;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::last_segment)
std::pair<int64_t, int64_t> TempoMap::lastSegment() const {
    int64_t curBar = 1, curTick = 0;
    for (size_t i = 0; i + 1 < meters_.size(); ++i) {
        const int64_t tpb = meters_[i].ticksPerBar();
        curBar = Time::satAdd(curBar, ceilDiv(Time::satSub(meters_[i + 1].tick, curTick), tpb));
        curTick = meters_[i + 1].tick;
    }
    return {curBar, curTick};
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::restore_meters_at_bars)
void TempoMap::restoreMetersAtBars(const std::vector<std::pair<int64_t, MeterEvent>>& later) {
    auto [curBar, curTick] = lastSegment();
    for (const auto& item : later) {
        const int64_t bar = item.first;
        const MeterEvent& ev = item.second;
        int64_t tick;
        if (bar >= curBar) {
            const MeterEvent last = meters_.empty() ? MeterEvent{0, 4, 4} : meters_.back();
            const int64_t tpb = last.ticksPerBar();
            tick = Time::satAdd(curTick, Time::satMul(Time::satSub(bar, curBar), tpb));
        } else {
            tick = barStartTick(bar);
        }
        MeterEvent placed = ev;
        placed.tick = tick;
        putMeter(placed);
        if (bar >= curBar) {
            curBar = bar;
            curTick = tick;
        } else {
            auto seg = lastSegment();
            curBar = seg.first;
            curTick = seg.second;
        }
    }
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::set_meter)
bool TempoMap::setMeter(int64_t tick, uint32_t num, uint32_t den) {
    if (!validMeter(num, den)) return false;
    if (tick <= 0)
        tick = 0;
    else
        tick = barStartTick(barBeatAtTick(tick).bar);
    auto later = takeMetersAfter(tick);
    putMeter({tick, num, den});
    restoreMetersAtBars(later);
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::remove_meter)
bool TempoMap::removeMeter(int64_t tick) {
    if (tick == 0) return false;
    const bool found = std::any_of(meters_.begin(), meters_.end(), [&](const MeterEvent& e) { return e.tick == tick; });
    if (!found) return false;
    auto later = takeMetersAfter(tick);
    meters_.erase(std::remove_if(meters_.begin(), meters_.end(), [&](const MeterEvent& e) { return e.tick == tick; }), meters_.end());
    restoreMetersAtBars(later);
    return true;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::scale_tempos)
bool TempoMap::scaleTempos(double factor) {
    auto out = tempos_;
    for (auto& e : out) {
        const double scaled = e.bpm * factor;
        if (!validBpm(scaled)) return false;
        e.bpm = scaled;
    }
    tempos_ = std::move(out);
    reindex();
    return true;
}

void TempoMap::segment(size_t i, int64_t& tick, double& bpm, double& elapsed) const {
    bpm = i < tempos_.size() ? tempos_[i].bpm : (tempos_.empty() ? 120.0 : tempos_.front().bpm);
    tick = i == 0 ? 0 : (i < tempos_.size() ? tempos_[i].tick : 0);
    elapsed = i < elapsed_.size() ? elapsed_[i] : 0.0;
}

double TempoMap::samplesPerTick(double bpm, SampleRate sr) { return sr.asF64() * 60.0 / (bpm * static_cast<double>(TICKS_PER_QUARTER)); }

double TempoMap::samplesPerElapsed(SampleRate sr) { return sr.asF64() * 60.0 / static_cast<double>(TICKS_PER_QUARTER); }

// after SoundCraft (crates/time/src/tempo.rs TempoMap::tempo_at_tick)
double TempoMap::tempoAtTick(int64_t tick) const {
    double bpm = tempos_.empty() ? 120.0 : tempos_.front().bpm;
    for (const auto& e : tempos_)
        if (e.tick <= tick)
            bpm = e.bpm;
        else
            break;
    return bpm;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::meter_at_tick)
MeterEvent TempoMap::meterAtTick(int64_t tick) const {
    MeterEvent m = meters_.empty() ? MeterEvent{0, 4, 4} : meters_.front();
    for (const auto& e : meters_)
        if (e.tick <= tick)
            m = e;
        else
            break;
    return m;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::tick_to_samples_f)
double TempoMap::tickToSamplesF(int64_t tick, SampleRate sr) const {
    size_t passed = 0;
    if (tempos_.size() > 1) {
        passed = static_cast<size_t>(
            std::partition_point(tempos_.begin() + 1, tempos_.end(), [&](const TempoEvent& e) { return e.tick < tick; }) -
            (tempos_.begin() + 1));
    }
    int64_t segTick = 0;
    double bpm = 120.0, elapsed = 0.0;
    segment(passed, segTick, bpm, elapsed);
    return elapsed * samplesPerElapsed(sr) + static_cast<double>(Time::satSub(tick, segTick)) * samplesPerTick(bpm, sr);
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::tick_to_samples)
Samples TempoMap::tickToSamples(int64_t tick, SampleRate sr) const { return toSamples(tickToSamplesF(tick, sr)); }

// after SoundCraft (crates/time/src/tempo.rs TempoMap::samples_to_ticks_f)
double TempoMap::samplesToTicksF(Samples s, SampleRate sr) const {
    const double sv = static_cast<double>(s);
    const double scale = samplesPerElapsed(sr);
    size_t passed = 0;
    if (elapsed_.size() > 1) {
        passed = static_cast<size_t>(
            std::partition_point(elapsed_.begin() + 1, elapsed_.end(), [&](double e) { return e * scale <= sv; }) -
            (elapsed_.begin() + 1));
    }
    int64_t segTick = 0;
    double bpm = 120.0, elapsed = 0.0;
    segment(passed, segTick, bpm, elapsed);
    return static_cast<double>(segTick) + (sv - elapsed * scale) / samplesPerTick(bpm, sr);
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::samples_to_ticks)
int64_t TempoMap::samplesToTicks(Samples s, SampleRate sr) const { return toSamples(samplesToTicksF(s, sr)); }

// after SoundCraft (crates/time/src/tempo.rs TempoMap::bar_start_tick)
int64_t TempoMap::barStartTick(int64_t bar) const {
    int64_t curBar = 1, curTick = 0;
    const MeterEvent first = meterAtTick(0);
    if (bar < 1) return Time::satMul(Time::satSub(bar, 1), first.ticksPerBar());
    for (size_t i = 0; i < meters_.size(); ++i) {
        const auto& m = meters_[i];
        const int64_t tpb = m.ticksPerBar();
        const std::optional<int64_t> nextTick = (i + 1 < meters_.size()) ? std::optional<int64_t>(meters_[i + 1].tick) : std::nullopt;
        if (nextTick) {
            const int64_t n = ceilDiv(Time::satSub(nextTick.value(), curTick), tpb);
            if (bar >= Time::satAdd(curBar, n)) {
                curBar = Time::satAdd(curBar, n);
                curTick = nextTick.value();
                continue;
            }
        }
        return Time::satAdd(curTick, Time::satMul(Time::satSub(bar, curBar), tpb));
    }
    return curTick;
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::bar_beat_at_tick)
BarBeat TempoMap::barBeatAtTick(int64_t tick) const {
    const MeterEvent first = meterAtTick(0);
    if (tick < 0) {
        const int64_t tpb = first.ticksPerBar();
        const uint64_t abs = Time::unsignedAbsI64(tick);
        const uint64_t tpbU = Time::unsignedAbsI64(tpb);
        const uint64_t barsBackU = abs / tpbU + static_cast<uint64_t>(abs % tpbU != 0);
        const int64_t barsBack = static_cast<int64_t>(std::min<uint64_t>(barsBackU, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
        const int64_t within = static_cast<int64_t>((tpbU - abs % tpbU) % tpbU);
        const int64_t beatT = first.ticksPerBeat();
        return {Time::satSub(1, barsBack), within / beatT + 1, within % beatT};
    }
    int64_t bar = 1, segTick = 0;
    for (size_t i = 0; i < meters_.size(); ++i) {
        const auto& m = meters_[i];
        const int64_t tpb = m.ticksPerBar();
        if (i + 1 < meters_.size() && tick >= meters_[i + 1].tick) {
            bar = Time::satAdd(bar, ceilDiv(Time::satSub(meters_[i + 1].tick, segTick), tpb));
            segTick = meters_[i + 1].tick;
            continue;
        }
        const int64_t rel = Time::satSub(tick, segTick);
        const int64_t bars = rel / tpb;
        const int64_t within = rel % tpb;
        const int64_t beatT = m.ticksPerBeat();
        return {Time::satAdd(bar, bars), within / beatT + 1, within % beatT};
    }
    return {bar, 1, 0};
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::tick_at_bar_beat)
int64_t TempoMap::tickAtBarBeat(const BarBeat& bb) const {
    const int64_t start = barStartTick(bb.bar);
    const MeterEvent m = meterAtTick(start);
    const int64_t beatOff = Time::satMul(std::max<int64_t>(0, Time::satSub(bb.beat, 1)), m.ticksPerBeat());
    return Time::satAdd(Time::satAdd(start, beatOff), bb.tick);
}

// after SoundCraft (crates/time/src/tempo.rs TempoMap::samples_at_bar_beat)
Samples TempoMap::samplesAtBarBeat(const BarBeat& bb, SampleRate sr) const { return tickToSamples(tickAtBarBeat(bb), sr); }

} // namespace Time
