// Offline consolidate: render a range to WAV beside the project (after SoundCraft crates/engine/src/io/render.rs consolidate).
#pragma once
#include "session.h"
#include "time_range.h"

#include <string>

namespace Consolidate {

bool track(Session::Document& doc, const std::string& trackId, Time::Range range, std::string& err);

} // namespace Consolidate