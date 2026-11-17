// Polyhymnia - splash: waveform bars and the name. Atelier greys and blue, solid colours only.
#include "splash.h"

#include <imgui.h>

#include <cfloat>
#include <cmath>

#ifndef POLYHYMNIA_VERSION
#define POLYHYMNIA_VERSION "0.1.0"
#endif

namespace Splash {

static ImFont *titleFont = nullptr, *smallFont = nullptr;
void setFonts(ImFont* title, ImFont* small) { titleFont = title; smallFont = small; }

static const ImU32 BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), LINE = IM_COL32(0x33, 0x33, 0x33, 255),
                   TEXT = IM_COL32(0xd6, 0xd6, 0xd6, 255), DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255);

static void text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* s) { dl->AddText(f ? f : ImGui::GetFont(), size, p, col, s); }
static float textWidth(ImFont* f, float size, const char* s) { return (f ? f : ImGui::GetFont())->CalcTextSizeA(size, FLT_MAX, 0, s).x; }

void draw(ImDrawList* dl, float x, float y, const char* status, float progress) {
    const ImVec2 o(x, y), end(x + W, y + H);
    dl->PushClipRect(o, end, true);
    dl->AddRectFilled(o, end, BG);
  // decorative waveform bars on the right
    const float bx = x + 400, by = y + 80, bw = 8, gap = 5;
    for (int i = 0; i < 24; ++i) {
        const float h = 40 + 120 * (0.35f + 0.65f * std::fabs(std::sin(i * 0.55f)));
        const float px = bx + i * (bw + gap);
        ImU32 c = (i % 7 == 0) ? BLUE : IM_COL32(0x55, 0x55, 0x55, 255);
        dl->AddRectFilled(ImVec2(px, by + 200 - h), ImVec2(px + bw, by + 200), c, 1);
    }
    const float ts = 44, tx = x + 40, ty = y + 100;
    text(dl, titleFont, ts, ImVec2(tx, ty), BLUE, "Polyhymnia");
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 58), DIM, "Version " POLYHYMNIA_VERSION);
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 80), DIM, "Arrange and mix your music");
    const float cs = 13, cry = y + 300;
    text(dl, smallFont, cs, ImVec2(tx, cry), DIM, "miniaudio, Dear ImGui, SDL2. After SoundCraft (storytold/soundcraft).");
    text(dl, smallFont, cs, ImVec2(tx, cry + 17), DIM, "Farica\xe2\x98\x86Kimora \xc2\xa9 Cursed Entertainment");
    if (status) text(dl, nullptr, 16, ImVec2(tx, y + 368), TEXT, status);
    if (progress >= 0) {
        const float p = progress > 1 ? 1 : progress;
        dl->AddRectFilled(ImVec2(x, end.y - 3), end, LINE);
        dl->AddRectFilled(ImVec2(x, end.y - 3), ImVec2(x + W * p, end.y), BLUE);
    }
    dl->AddRect(o, end, LINE);
    dl->PopClipRect();
}

}