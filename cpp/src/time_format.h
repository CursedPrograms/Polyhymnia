// Counter timebases: format and parse (after SoundCraft crates/time/src/format.rs).
#pragma once
#include "session.h"
#include "time_samples.h"

#include <optional>
#include <string>

namespace TimeFormat {

enum class Kind { BarsBeats, MinSecs, Timecode, FeetFrames, Samples };

const char* label(Kind k);
Kind allKinds(size_t i);

std::string formatPosition(long long s, Kind fmt, const Session::Document& doc);
std::string formatLength(long long start, long long len, Kind fmt, const Session::Document& doc);
std::optional<long long> parsePosition(const std::string& text, Kind fmt, const Session::Document& doc);

} // namespace TimeFormat