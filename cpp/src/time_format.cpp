// Format and parse timeline positions in each timebase (after SoundCraft crates/time/src/format.rs).
#include "time_format.h"
#include "clip_ops.h"
#include "engine_edit.h"
#include "tempo_map.h"
#include "time_grid.h"
#include "time_range.h"
#include "time_wide.h"
#include "timecode.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace TimeFormat {

static Time::SampleRate sr(const Session::Document& doc) {
    return Time::SampleRate::fromHz(static_cast<uint32_t>(std::max(1.0, doc.sampleRate)));
}

static bool ieqAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

static std::string_view trimSv(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return sv;
}

static bool parseStrictI64(std::string_view sv, int64_t& out) {
    sv = trimSv(sv);
    if (sv.empty() || sv.front() == '+') return false;
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

static bool parseStrictF64(std::string_view sv, double& out) {
    sv = trimSv(sv);
    if (sv.empty()) return false;
    std::string s(sv);
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size();
}

// after SoundCraft (crates/time/src/format.rs TimeFormat::label)
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

// after SoundCraft (crates/time/src/format.rs TimeFormat::id)
const char* id(Kind k) {
    switch (k) {
    case Kind::BarsBeats: return "bars_beats";
    case Kind::MinSecs: return "min_secs";
    case Kind::Timecode: return "timecode";
    case Kind::FeetFrames: return "feet_frames";
    case Kind::Samples: return "samples";
    }
    return "min_secs";
}

// after SoundCraft (crates/time/src/format.rs TimeFormat::from_id)
std::optional<Kind> fromId(const std::string& s) {
    for (Kind k : ALL) {
        if (s == id(k) || ieqAscii(s, label(k))) return k;
    }
    return std::nullopt;
}

Kind allKinds(size_t i) { return ALL[i % 5]; }

// after SoundCraft (crates/time/src/format.rs min_secs)
static std::string minSecs(long long s, Time::SampleRate rate) {
    const bool neg = s < 0;
    const int64_t hz = std::max<int64_t>(1, static_cast<int64_t>(rate.hz));
    const Time::Int128 msTotal128 = (Time::Int128::unsignedAbs(s) * 1000) / Time::Int128(hz);
    const int64_t msTotal = msTotal128.saturatingToI64();
    const int64_t mins = msTotal / 60000;
    const int64_t secs = (msTotal / 1000) % 60;
    const int64_t ms = msTotal % 1000;
    char buf[48];
    snprintf(buf, sizeof(buf), "%s%lld:%02lld.%03lld", neg ? "-" : "",
             static_cast<long long>(mins), static_cast<long long>(secs), static_cast<long long>(ms));
    return buf;
}

// after SoundCraft (crates/time/src/format.rs format_position)
std::string formatPosition(long long s, Kind fmt, Time::SampleRate rate, const Time::TempoMap& map, Time::FrameRate frameRate,
                           long long tcStart, int64_t ffStart) {
    switch (fmt) {
    case Kind::Samples: {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld", s);
        return buf;
    }
    case Kind::MinSecs:
        return minSecs(s, rate);
    case Kind::Timecode:
        return Time::Timecode::fromSamples(Time::satAdd(s, tcStart), frameRate, rate).formatLabel(frameRate);
    case Kind::FeetFrames: {
        int64_t ft = 0, fr = 0;
        Time::feetFramesParts(s, frameRate, rate, ffStart, ft, fr);
        char buf[48];
        snprintf(buf, sizeof(buf), "%lld+%02lld", static_cast<long long>(ft), static_cast<long long>(fr));
        return buf;
    }
    case Kind::BarsBeats: {
        const auto bb = map.barBeatAt(s, rate);
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld| %lld| %03lld",
                 static_cast<long long>(bb.bar), static_cast<long long>(bb.beat), static_cast<long long>(bb.tick));
        return buf;
    }
    }
    return minSecs(s, rate);
}

std::string formatPosition(long long s, Kind fmt, const Session::Document& doc) {
    return formatPosition(s, fmt, sr(doc), TimeGrid::tempoMapFromSession(doc), doc.frameRate, doc.timecodeStart, doc.feetFramesStart);
}

// after SoundCraft (crates/time/src/format.rs format_length)
std::string formatLength(long long start, long long len, Kind fmt, Time::SampleRate rate, const Time::TempoMap& map,
                         Time::FrameRate frameRate) {
    if (fmt == Kind::BarsBeats) {
        const int64_t t0 = map.samplesToTicks(start, rate);
        const int64_t t1 = map.samplesToTicks(Time::satAdd(start, len), rate);
        const auto m = map.meterAtTick(t0);
        const int64_t d = std::max<int64_t>(0, Time::satSub(t1, t0));
        const int64_t tpb = std::max<int64_t>(1, m.ticksPerBar());
        const int64_t tpbBeat = std::max<int64_t>(1, m.ticksPerBeat());
        const int64_t bars = d / tpb;
        const int64_t rem = d % tpb;
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld| %lld| %03lld",
                 static_cast<long long>(bars), static_cast<long long>(rem / tpbBeat), static_cast<long long>(rem % tpbBeat));
        return buf;
    }
    if (fmt == Kind::Timecode)
        return Time::Timecode::fromSamples(len, frameRate, rate).formatLabel(frameRate);
    return formatPosition(len, fmt, rate, map, frameRate, 0, 0);
}

std::string formatLength(long long start, long long len, Kind fmt, const Session::Document& doc) {
    return formatLength(start, len, fmt, sr(doc), TimeGrid::tempoMapFromSession(doc), doc.frameRate);
}

// after SoundCraft (crates/time/src/format.rs parse_position)
std::optional<long long> parsePosition(const std::string& text, Kind fmt, Time::SampleRate rate, const Time::TempoMap& map,
                                       Time::FrameRate frameRate, long long tcStart, int64_t ffStart) {
    std::string_view t = trimSv(text);
    switch (fmt) {
    case Kind::Samples: {
        std::string n;
        n.reserve(t.size());
        for (char c : t) {
            if (c == ',' || c == '_') continue;
            n.push_back(c);
        }
        int64_t v = 0;
        if (!parseStrictI64(n, v)) return std::nullopt;
        return v;
    }
    case Kind::MinSecs: {
        bool neg = false;
        if (!t.empty() && t.front() == '-') {
            neg = true;
            t.remove_prefix(1);
        }
        const size_t colon = t.find(':');
        double mins = 0.0, secs = 0.0;
        if (colon != std::string_view::npos) {
            if (!parseStrictF64(t.substr(0, colon), mins) || !parseStrictF64(t.substr(colon + 1), secs)) return std::nullopt;
        } else {
            if (!parseStrictF64(t, secs)) return std::nullopt;
        }
        if (!std::isfinite(mins) || !std::isfinite(secs) || mins < 0.0 || secs < 0.0) return std::nullopt;
        const long long v = rate.samples(mins * 60.0 + secs);
        return neg ? Time::satNeg(v) : v;
    }
    case Kind::Timecode: {
        Time::Timecode tc;
        if (!Time::Timecode::parse(std::string(t), tc)) return std::nullopt;
        const int64_t base = std::max<int64_t>(1, Time::frameNominal(frameRate));
        if (tc.minutes >= 60 || tc.seconds >= 60 || tc.frames >= base) return std::nullopt;
        if (Time::frameIsDrop(frameRate) && tc.seconds == 0 && tc.minutes % 10 != 0) {
            const int64_t drop = base >= 60 ? 4 : 2;
            if (tc.frames < drop) return std::nullopt;
        }
        return Time::satSub(tc.toSamples(frameRate, rate), tcStart);
    }
    case Kind::FeetFrames: {
        const size_t plus = t.find('+');
        if (plus == std::string_view::npos) return std::nullopt;
        int64_t ft = 0, fr = 0;
        if (!parseStrictI64(t.substr(0, plus), ft) || !parseStrictI64(t.substr(plus + 1), fr)) return std::nullopt;
        const int64_t frameIndex = Time::satSub(Time::satAdd(Time::satMul(ft, 16), fr), ffStart);
        return Time::samplesAtFrame(frameRate, frameIndex, rate);
    }
    case Kind::BarsBeats: {
        std::vector<int64_t> parts;
        size_t start = 0;
        for (size_t i = 0; i <= t.size(); ++i) {
            if (i == t.size() || t[i] == '|' || t[i] == '.' || t[i] == ' ') {
                const std::string_view piece = t.substr(start, i - start);
                if (!piece.empty()) {
                    int64_t v = 0;
                    if (!parseStrictI64(piece, v)) return std::nullopt;
                    parts.push_back(v);
                }
                start = i + 1;
            }
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

std::optional<long long> parsePosition(const std::string& text, Kind fmt, const Session::Document& doc) {
    return parsePosition(text, fmt, sr(doc), TimeGrid::tempoMapFromSession(doc), doc.frameRate, doc.timecodeStart, doc.feetFramesStart);
}

} // namespace TimeFormat
