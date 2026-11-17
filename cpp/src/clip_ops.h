// Clip trim, range, gain (after SoundCraft crates/model/src/clip.rs impl Clip).
#pragma once
#include "session.h"
#include "time_range.h"

#include <optional>

namespace ClipOps {

constexpr long long kMaxPosition = 1LL << 40;

long long end(const Session::Clip& c);
Time::Range range(const Session::Clip& c);
long long sourceOffset(const Session::Clip& c);
float envDbAt(const Session::Clip& c, long long rel);
float gainAt(const Session::Clip& c, long long rel);
float gainAtWithFades(const Session::Clip& c, long long rel, Session::Fade fadeIn, Session::Fade fadeOut);
float fadeShapeGain(Session::FadeShape shape, float t);
float fadeGain(const Session::Fade& f, float t);
float fadeCurvatureForMidpointGain(const Session::Fade& f, float gain);
void fadeSanitize(Session::Fade& f);
const char* fadeShapeLabel(Session::FadeShape shape);
std::optional<Session::FadeShape> fadeShapeFromId(const std::string& s);
void trimStartTo(Session::Clip& c, long long newStart);
void trimEndTo(Session::Clip& c, long long newEnd);
void clampFades(Session::Clip& c);
bool locked(const Session::Clip& c);

} // namespace ClipOps