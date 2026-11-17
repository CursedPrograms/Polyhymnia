// Format and parse timeline positions in each timebase.
#include "time_format.h"
#include "clip_ops.h"
#include "engine_edit.h"
#include "tempo_map.h"
#include "time_grid.h"
#include "time_range.h"
#include "timecode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace TimeFormat {

static Time::SampleRate sr(const Session::Document& doc) { return Time::SampleRate::fromHz((uint32_t)std::max(1.0, doc.sampleRate)); }

const char* label(Kind k) {
    switch (k) {
    case Kind::BarsBeats: return "Bars|Beats";
    case Kind::MinSecs: return "Min:Secs";
    case Kind::Timecode: return "Timecode";
    case Kind::FeetFrames: return "Feet+Frames";
    case Kind::Samples: return "Samples";
    }
    return "Min:Secs";
}

Kind allKinds(size_t i) {
    static const Kind kAll[] = {Kind::BarsBeats, Kind::MinSecs, Kind::Timecode, Kind::FeetFrames, Kind::Samples};
    return kAll[i % (sizeof(kAll) / sizeof(kAll[0]))];
}

// after SoundCraft format.rs min_secs
static std::string minSecs(long long s, Time::SampleRate rate) {
    const bool neg = s < 0;
    const int64_t hz = std::max<int64_t>(1, (int64_t)rate.hz);
    const int64_t msTotal = (int64_t)(std::abs((long double)s) * 1000LL / hz);
    const int64_t mins = msTotal / 60000;
    const int64_t secs = (msTotal / 1000) % 60;
    const int64_t ms = msTotal % 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%lld:%02lld.%03lld", neg ? "-" : "", (long long)mins, (long long)secs, (long long)ms);
    return buf;
}

std::string formatPosition(long long s, Kind fmt, const Session::Document& doc) {
    const auto rate = sr(doc);
    const auto map = TimeGrid::tempoMapFromSession(doc);
    switch (fmt) {
    case Kind::Samples: {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld", (long long)s);
        return buf;
    }
    case Kind::MinSecs:
        return minSecs(s, rate);
    case Kind::Timecode:
        return Time::Timecode::fromSamples(Time::satAdd(s, doc.timecodeStart), doc.frameRate, rate).formatLabel(doc.frameRate);
    case Kind::FeetFrames: {
        int64_t ft = 0, fr = 0;
        Time::feetFramesParts(s, doc.frameRate, rate, doc.feetFramesStart, ft, fr);
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld+%02lld", (long long)ft, (long long)fr);
        return buf;
    }
    case Kind::BarsBeats: {
        const auto bb = map.barBeatAt(s, rate);
        char buf[48];
        snprintf(buf, sizeof(buf), "%lld| %lld| %03lld", (long long)bb.bar, (long long)bb.beat, (long long)bb.tick);
        return buf;
    }
    }
    return minSecs(s, rate);
}

std::string formatLength(long long start, long long len, Kind fmt, const Session::Document& doc) {
    const auto rate = sr(doc);
    const auto map = TimeGrid::tempoMapFromSession(doc);
    if (fmt == Kind::BarsBeats) {
        const int64_t t0 = map.samplesToTicks(start, rate);
        const int64_t t1 = map.samplesToTicks(Time::satAdd(start, len), rate);
        const auto m = map.meterAtTick(t0);
        const int64_t d = std::max<int64_t>(0, t1 - t0);
        const int64_t tpb = std::max<int64_t>(1, m.ticksPerBar());
        const int64_t tpbBeat = std::max<int64_t>(1, m.ticksPerBeat());
        const int64_t bars = d / tpb;
        const int64_t rem = d % tpb;
        char buf[48];
        snprintf(buf, sizeof(buf), "%lld| %lld| %03lld", (long long)bars, (long long)(rem / tpbBeat), (long long)(rem % tpbBeat));
        return buf;
    }
    if (fmt == Kind::Timecode)
        return Time::Timecode::fromSamples(len, doc.frameRate, rate).formatLabel(doc.frameRate);
    return formatPosition(len, fmt, doc);
}

std::optional<long long> parsePosition(const std::string& text, Kind fmt, const Session::Document& doc) {
    std::string t = text;
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
    const auto rate = sr(doc);
    const auto map = TimeGrid::tempoMapFromSession(doc);
    switch (fmt) {
    case Kind::Samples: {
        try {
            std::string n;
            for (char c : t) {
                if (c == ',' || c == '_') continue;
                n.push_back(c);
            }
            size_t idx = 0;
            const long long v = std::stoll(n, &idx);
            if (idx != n.size()) return std::nullopt;
            return v;
        } catch (...) {
            return std::nullopt;
        }
    }
    case Kind::MinSecs: {
        bool neg = false;
        if (!t.empty() && t.front() == '-') {
            neg = true;
            t = t.substr(1);
        }
        size_t colon = t.find(':');
        double mins = 0.0, secs = 0.0;
        try {
            if (colon != std::string::npos) {
                mins = std::stod(t.substr(0, colon));
                secs = std::stod(t.substr(colon + 1));
            } else
                secs = std::stod(t);
        } catch (...) {
            return std::nullopt;
        }
        if (!std::isfinite(mins) || !std::isfinite(secs) || mins < 0.0 || secs < 0.0) return std::nullopt;
        const long long v = rate.samples(mins * 60.0 + secs);
        return neg ? -v : v;
    }
    case Kind::Timecode: {
        Time::Timecode tc;
        if (!Time::Timecode::parse(t, tc)) return std::nullopt;
        const int64_t base = std::max<int64_t>(1, Time::frameNominal(doc.frameRate));
        if (tc.minutes >= 60 || tc.seconds >= 60 || tc.frames >= base) return std::nullopt;
        if (Time::frameIsDrop(doc.frameRate) && tc.seconds == 0 && tc.minutes % 10 != 0) {
            const int64_t drop = base >= 60 ? 4 : 2;
            if (tc.frames < drop) return std::nullopt;
        }
        return Time::satSub(tc.toSamples(doc.frameRate, rate), doc.timecodeStart);
    }
    case Kind::FeetFrames: {
        const size_t plus = t.find('+');
        if (plus == std::string::npos) return std::nullopt;
        try {
            const long long ft = std::stoll(t.substr(0, plus));
            const long long fr = std::stoll(t.substr(plus + 1));
            const long long frameIndex = Time::satSub(Time::satAdd(ft * 16, fr), doc.feetFramesStart);
            return Time::samplesAtFrame(doc.frameRate, frameIndex, rate);
        } catch (...) {
            return std::nullopt;
        }
    }
    case Kind::BarsBeats: {
        std::vector<long long> parts;
        std::string cur;
        for (size_t i = 0; i <= t.size(); ++i) {
            const char c = i < t.size() ? t[i] : '|';
            if (c == '|' || c == '.' || c == ' ') {
                if (!cur.empty()) {
                    try {
                        parts.push_back(std::stoll(cur));
                    } catch (...) {
                        return std::nullopt;
                    }
                    cur.clear();
                }
            } else
                cur.push_back(c);
        }
        if (parts.empty()) return std::nullopt;
        Time::BarBeat bb;
        bb.bar = parts[0];
        bb.beat = parts.size() > 1 ? std::max<int64_t>(1, parts[1]) : 1;
        bb.tick = parts.size() > 2 ? std::max<int64_t>(0, parts[2]) : 0;
        return map.samplesAtBarBeat(bb, rate);
    }
    }
    return std::nullopt;
}

} // namespace TimeFormat