// Polyhymnia - splash screen while the app starts, and Help > About.
#pragma once

struct ImDrawList;
struct ImFont;

namespace Splash {
constexpr float W = 640, H = 400;
void setFonts(ImFont* title, ImFont* small);
void draw(ImDrawList* dl, float x, float y, const char* status, float progress);
}