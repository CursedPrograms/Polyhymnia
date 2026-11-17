// Counter timebases: format and parse (after SoundCraft crates/time/src/format.rs).
#pragma once
#include "session.h"
#include "tempo_map.h"
#include "time_samples.h"
#include "timecode.h"

#include <optional>
#include <string>

namespace TimeFormat {

enum class Kind { BarsBeats, MinSecs, Timecode, FeetFrames, Samples };

constexpr Kind ALL[5] = {Kind::BarsBeats, Kind::MinSecs, Kind::Timecode, Kind::FeetFrames, Kind::Samples};

const char* label(Kind k);
const char* id(Kind k);
std::optional<Kind> fromId(const std::string& s);
Kind allKinds(size_t i);

std::string formatPosition(long long s, Kind fmt, Time::SampleRate sr, const Time::TempoMap& map, Time::FrameRate rate,
                           long long tcStart, int64_t ffStart);
std::string formatPosition(long long s, Kind fmt, const Session::Document& doc);

std::string formatLength(long long start, long long len, Kind fmt, Time::SampleRate sr, const Time::TempoMap& map,
                         Time::FrameRate rate);
std::string formatLength(long long start, long long len, Kind fmt, const Session::Document& doc);

std::optional<long long> parsePosition(const std::string& text, Kind fmt, Time::SampleRate sr, const Time::TempoMap& map,
                                       Time::FrameRate rate, long long tcStart, int64_t ffStart);
std::optional<long long> parsePosition(const std::string& text, Kind fmt, const Session::Document& doc);

} // namespace TimeFormat
