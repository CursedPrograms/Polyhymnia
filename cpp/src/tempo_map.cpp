// Tempo map tick/sample conversion.
#include "tempo_map.h"
#include "time_range.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace Time {

static int64_t ceilDiv(int64_t num, int64_t den) {
    if (den <= 0) return 0;
    return num / den + static_cast<int64_t>(num % den != 0);
}

int64_t MeterEvent::ticksPerBeat() const {
    const int64_t d = std::max<int64_t>(1, static_cast<int64_t>(denominator));
    return std::max<int64_t>(1, TICKS_PER_QUARTER * 4 / d);
}

int64_t MeterEvent::ticksPerBar() const { return ticksPerBeat() * std::max<int64_t>(1, static_cast<int64_t>(numerator)); }

TempoMap TempoMap::defaultMap() {
    TempoMap m;
    m.tempos_.push_back({0, 120.0});
    m.meters_.push_back({0, 4, 4});
    m.reindex();
    return m;
}

TempoMap TempoMap::make(double bpm, uint32_t num, uint32_t den) {
    TempoMap m = defaultMap();
    m.setTempo(0, bpm);
    m.setMeter(0, num, den);
    return m;
}

void TempoMap::reindex() {
    elapsed_.clear();
    double acc = 0.0;
    int64_t segTick = 0;
    double bpm = tempos_.empty() ? 120.0 : tempos_.front().bpm;
    for (size_t i = 0; i < tempos_.size(); ++i) {
        const auto& e = tempos_[i];
        if (i > 0) {
            acc += static_cast<double>(e.tick - segTick) / bpm;
            segTick = e.tick;
            bpm = e.bpm;
        }
        elapsed_.push_back(acc);
    }
}

bool TempoMap::setTempo(int64_t tick, double bpm) {
    if (!std::isfinite(bpm) || bpm < 5.0 || bpm > 1000.0) return false;
    tick = std::max<int64_t>(0, tick);
    const auto it = std::lower_bound(tempos_.begin(), tempos_.end(), tick, [](const TempoEvent& e, int64_t t) { return e.tick < t; });
    if (it != tempos_.end() && it->tick == tick)
        const_cast<TempoEvent&>(*it).bpm = bpm;
    else
        tempos_.insert(it, {tick, bpm});
    reindex();
    return true;
}

void TempoMap::putMeter(const MeterEvent& ev) {
    const auto it = std::lower_bound(meters_.begin(), meters_.end(), ev.tick, [](const MeterEvent& e, int64_t t) { return e.tick < t; });
    if (it != meters_.end() && it->tick == ev.tick)
        const_cast<MeterEvent&>(*it) = ev;
    else
        meters_.insert(it, ev);
}

std::vector<std::pair<int64_t, MeterEvent>> TempoMap::takeMetersAfter(int64_t tick) {
    std::vector<std::pair<int64_t, MeterEvent>> later;
    int64_t bar = 1, segTick = 0;
    for (size_t i = 0; i < meters_.size(); ++i) {
        const auto& m = meters_[i];
        const int64_t tpb = m.ticksPerBar();
        if (m.tick > tick) later.push_back({bar + (m.tick - segTick) / tpb, m});
        if (i + 1 < meters_.size()) {
            const int64_t nt = meters_[i + 1].tick;
            bar = bar + ceilDiv(nt - segTick, tpb);
            segTick = nt;
        }
    }
    meters_.erase(std::remove_if(meters_.begin(), meters_.end(), [&](const MeterEvent& e) { return e.tick > tick; }), meters_.end());
    return later;
}

std::pair<int64_t, int64_t> TempoMap::lastSegment() const {
    int64_t curBar = 1, curTick = 0;
    for (size_t i = 0; i + 1 < meters_.size(); ++i) {
        const int64_t tpb = meters_[i].ticksPerBar();
        curBar = curBar + ceilDiv(meters_[i + 1].tick - curTick, tpb);
        curTick = meters_[i + 1].tick;
    }
    return {curBar, curTick};
}

void TempoMap::restoreMetersAtBars(const std::vector<std::pair<int64_t, MeterEvent>>& later) {
    auto [curBar, curTick] = lastSegment();
    for (const auto& item : later) {
        const int64_t bar = item.first;
        const MeterEvent& ev = item.second;
        int64_t tick;
        if (bar >= curBar) {
            const MeterEvent last = meters_.empty() ? MeterEvent{} : meters_.back();
            const int64_t tpb = last.ticksPerBar();
            tick = curTick + (bar - curBar) * tpb;
        } else
            tick = barStartTick(bar);
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

bool TempoMap::setMeter(int64_t tick, uint32_t num, uint32_t den) {
    if (num < 1 || num > 64) return false;
    static const uint32_t validDen[] = {1, 2, 4, 8, 16, 32, 64};
    if (std::find(std::begin(validDen), std::end(validDen), den) == std::end(validDen)) return false;
    if (tick <= 0) tick = 0;
    else tick = barStartTick(barBeatAtTick(tick).bar);
    auto later = takeMetersAfter(tick);
    putMeter({tick, num, den});
    restoreMetersAtBars(later);
    return true;
}

void TempoMap::segment(size_t i, int64_t& tick, double& bpm, double& elapsed) const {
    bpm = i < tempos_.size() ? tempos_[i].bpm : (tempos_.empty() ? 120.0 : tempos_.front().bpm);
    tick = i == 0 ? 0 : (i < tempos_.size() ? tempos_[i].tick : 0);
    elapsed = i < elapsed_.size() ? elapsed_[i] : 0.0;
}

double TempoMap::samplesPerTick(double bpm, SampleRate sr) { return sr.asF64() * 60.0 / (bpm * static_cast<double>(TICKS_PER_QUARTER)); }

double TempoMap::samplesPerElapsed(SampleRate sr) { return sr.asF64() * 60.0 / static_cast<double>(TICKS_PER_QUARTER); }

double TempoMap::tempoAtTick(int64_t tick) const {
    double bpm = tempos_.empty() ? 120.0 : tempos_.front().bpm;
    for (const auto& e : tempos_)
        if (e.tick <= tick)
            bpm = e.bpm;
        else
            break;
    return bpm;
}

MeterEvent TempoMap::meterAtTick(int64_t tick) const {
    MeterEvent m{0, 4, 4};
    for (const auto& e : meters_)
        if (e.tick <= tick)
            m = e;
        else
            break;
    return m;
}

double TempoMap::tickToSamplesF(int64_t tick, SampleRate sr) const {
    size_t passed = 0;
    if (tempos_.size() > 1) {
        passed = static_cast<size_t>(
            std::partition_point(tempos_.begin() + 1, tempos_.end(), [&](const TempoEvent& e) { return e.tick < tick; }) - tempos_.begin());
        if (passed) --passed;
    }
    int64_t segTick = 0;
    double bpm = 120.0, elapsed = 0.0;
    segment(passed, segTick, bpm, elapsed);
    return elapsed * samplesPerElapsed(sr) + static_cast<double>(tick - segTick) * samplesPerTick(bpm, sr);
}

Samples TempoMap::tickToSamples(int64_t tick, SampleRate sr) const { return toSamples(tickToSamplesF(tick, sr)); }

double TempoMap::samplesToTicksF(Samples s, SampleRate sr) const {
    const double sv = static_cast<double>(s);
    const double scale = samplesPerElapsed(sr);
    size_t passed = 0;
    if (elapsed_.size() > 1) {
        passed = static_cast<size_t>(std::partition_point(elapsed_.begin() + 1, elapsed_.end(), [&](double e) { return e * scale <= sv; }) -
                                     elapsed_.begin());
        if (passed) --passed;
    }
    int64_t segTick = 0;
    double bpm = 120.0, elapsed = 0.0;
    segment(passed, segTick, bpm, elapsed);
    return static_cast<double>(segTick) + (sv - elapsed * scale) / samplesPerTick(bpm, sr);
}

int64_t TempoMap::samplesToTicks(Samples s, SampleRate sr) const { return toSamples(samplesToTicksF(s, sr)); }

int64_t TempoMap::barStartTick(int64_t bar) const {
    int64_t curBar = 1, curTick = 0;
    const MeterEvent first = meterAtTick(0);
    if (bar < 1) return (bar - 1) * first.ticksPerBar();
    for (size_t i = 0; i < meters_.size(); ++i) {
        const auto& m = meters_[i];
        const int64_t tpb = m.ticksPerBar();
        const std::optional<int64_t> nextTick = (i + 1 < meters_.size()) ? std::optional<int64_t>(meters_[i + 1].tick) : std::nullopt;
        if (nextTick) {
            const int64_t n = ceilDiv(nextTick.value() - curTick, tpb);
            if (bar >= curBar + n) {
                curBar = curBar + n;
                curTick = nextTick.value();
                continue;
            }
        }
        return curTick + (bar - curBar) * tpb;
    }
    return curTick;
}

BarBeat TempoMap::barBeatAtTick(int64_t tick) const {
    const MeterEvent first = meterAtTick(0);
    if (tick < 0) {
        const int64_t tpb = first.ticksPerBar();
        const uint64_t abs = static_cast<uint64_t>(tick < 0 ? -tick : tick);
        const uint64_t tpbU = static_cast<uint64_t>(tpb);
        const uint64_t barsBackU = abs / tpbU + static_cast<uint64_t>(abs % tpbU != 0);
        const int64_t barsBack = static_cast<int64_t>(std::min<uint64_t>(barsBackU, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
        const int64_t within = static_cast<int64_t>(((tpbU - abs % tpbU) % tpbU));
        const int64_t beatT = first.ticksPerBeat();
        return {1 - barsBack, within / beatT + 1, within % beatT};
    }
    int64_t bar = 1, segTick = 0;
    for (size_t i = 0; i < meters_.size(); ++i) {
        const auto& m = meters_[i];
        const int64_t tpb = m.ticksPerBar();
        if (i + 1 < meters_.size() && tick >= meters_[i + 1].tick) {
            bar = bar + ceilDiv(meters_[i + 1].tick - segTick, tpb);
            segTick = meters_[i + 1].tick;
            continue;
        }
        const int64_t rel = tick - segTick;
        const int64_t bars = rel / tpb;
        const int64_t within = rel % tpb;
        const int64_t beatT = m.ticksPerBeat();
        return {bar + bars, within / beatT + 1, within % beatT};
    }
    return {bar, 1, 0};
}

int64_t TempoMap::tickAtBarBeat(const BarBeat& bb) const {
    const int64_t start = barStartTick(bb.bar);
    const MeterEvent m = meterAtTick(start);
    const int64_t beatOff = std::max<int64_t>(0, bb.beat - 1) * m.ticksPerBeat();
    return Time::satAdd(Time::satAdd(start, beatOff), bb.tick);
}

Samples TempoMap::samplesAtBarBeat(const BarBeat& bb, SampleRate sr) const { return tickToSamples(tickAtBarBeat(bb), sr); }

} // namespace Time