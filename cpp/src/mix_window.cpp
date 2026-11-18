// Polyhymnia - Mix window: channel strips, inserts, sends, routing, faders, meters, and plugin editor windows
// (after SoundCraft crates/ui-egui/src/mix_window.rs, mix_window/*.rs, panels/plugins.rs, widgets.rs).
#include "mix_window.h"
#include "app.h"
#include "dsp.h"
#include "editor.h"
#include "icons.h"
#include "lang.h"
#include "playback.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace MixWindow {

namespace {

bool gNarrowMix = false;
std::vector<std::pair<uint64_t, size_t>> gOpenPluginWindows;
std::optional<std::pair<uint64_t, size_t>> gOpenSendWindow;

// Studio tokens matching SoundCraft crates/ui-egui/src/theme.rs
const ImU32 C_STRIP_BG = IM_COL32(0x28, 0x28, 0x2b, 255);
const ImU32 C_STRIP_SEL = IM_COL32(0x32, 0x34, 0x39, 255);
const ImU32 C_SECTION = IM_COL32(0x1f, 0x1f, 0x22, 255);
const ImU32 C_SLOT_EMPTY = IM_COL32(0x17, 0x17, 0x19, 255);
const ImU32 C_SLOT_EMPTY_HI = IM_COL32(0x22, 0x22, 0x26, 255);
const ImU32 C_SLOT_FACE = IM_COL32(0x2d, 0x45, 0x5c, 255);
const ImU32 C_SLOT_SEND = IM_COL32(0x25, 0x4a, 0x36, 255);
const ImU32 C_SLOT_BYP = IM_COL32(0x3a, 0x3a, 0x3e, 255);
const ImU32 C_LCD = IM_COL32(0x0f, 0x13, 0x11, 255);
const ImU32 C_LCD_TEXT = IM_COL32(0x7c, 0xf2, 0x9c, 255);
const ImU32 C_BORDER = IM_COL32(0x0c, 0x0c, 0x0c, 255);
const ImU32 C_EDGE_LIGHT = IM_COL32(0x3c, 0x3c, 0x40, 255);
const ImU32 C_TEXT = IM_COL32(0xe2, 0xe2, 0xe2, 255);
const ImU32 C_DIM = IM_COL32(0x96, 0x96, 0x96, 255);
const ImU32 C_SEC_LABEL = IM_COL32(0xb8, 0xb8, 0xbc, 255);
const ImU32 C_BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255);
const ImU32 C_REC = IM_COL32(0xe6, 0x32, 0x32, 255);
const ImU32 C_SOLO = IM_COL32(0xe8, 0xc2, 0x28, 255);
const ImU32 C_MUTE = IM_COL32(0xe0, 0x7a, 0x22, 255);
const ImU32 C_METER_BG = IM_COL32(0x0b, 0x0d, 0x0c, 255);
const ImU32 C_METER_GREEN = IM_COL32(0x28, 0xc8, 0x54, 255);
const ImU32 C_METER_YELLOW = IM_COL32(0xf0, 0xc8, 0x28, 255);
const ImU32 C_METER_RED = IM_COL32(0xe6, 0x32, 0x32, 255);
const ImU32 C_NAME_PLATE = IM_COL32(0xc8, 0xc8, 0xcc, 255);
const ImU32 C_NAME_PLATE_SEL = IM_COL32(0xee, 0xee, 0xf2, 255);
const ImU32 C_NAME_TEXT = IM_COL32(0x14, 0x14, 0x16, 255);

std::string trimAscii(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string toLowerAscii(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

void syncInsertsSummary(Session::Track& track) {
    std::string sum;
    for (const auto& slot : track.mixer.inserts) {
        if (!slot.has_value()) continue;
        const Dsp::PluginInfo* inf = Dsp::pluginInfo(slot->plugin);
        const std::string sn = inf ? inf->shortName : slot->plugin;
        if (!sum.empty()) sum += " \xc2\xb7 ";
        sum += sn;
    }
    track.insertsSummary = sum;
}

void drawInsetBox(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 fill) {
    dl->AddRectFilled(mn, mx, fill, 2.0f);
    dl->AddRect(mn, mx, C_BORDER, 2.0f);
    dl->AddLine(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 1.0f, mn.y + 1.0f), IM_COL32(0, 0, 0, 70));
}

void drawSectionHeader(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const char* title, bool narrow) {
    const char* text = title;
    if (narrow) {
        if (std::strcmp(title, "INSERTS A-E") == 0) text = "I A-E";
        else if (std::strcmp(title, "INSERTS F-J") == 0) text = "I F-J";
        else if (std::strcmp(title, "SENDS A-E") == 0) text = "S A-E";
        else if (std::strcmp(title, "SENDS F-J") == 0) text = "S F-J";
        else if (std::strcmp(title, "INSTRUMENT") == 0) text = "INST";
    }
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * 0.76f;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    dl->AddText(font, fs, ImVec2((mn.x + mx.x - ts.x) * 0.5f, mn.y + 2.0f), C_SEC_LABEL, text);
}

// Rotary pan knob + console readout (after widgets.rs pan_knob)
bool drawPanKnob(const char* id, ImVec2 center, float size, float& val, const char* tooltip) {
    const float half = size * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(center.x - half, center.y - half));
    ImGui::InvisibleButton(id, ImVec2(size, size));
    bool changed = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        const float next = std::clamp(val + (d.x - d.y) * 0.01f, -1.0f, 1.0f);
        if (std::abs(next - val) > 1e-5f) {
            val = next;
            changed = true;
        }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        val = 0.0f;
        changed = true;
    }
    if (ImGui::IsItemHovered() && tooltip) {
        ImGui::SetTooltip("%s", tooltip);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rad = size * 0.40f;
    constexpr float kPi = 3.14159265f;
    const float start = kPi * 0.75f;
    const float sweep = kPi * 1.5f;
    const float mid = start + sweep * 0.5f;
    const float at = start + sweep * (val * 0.5f + 0.5f);

    // Background arc
    ImVec2 bgArc[25];
    for (int i = 0; i <= 24; ++i) {
        const float a = start + sweep * (float)i / 24.0f;
        bgArc[i] = ImVec2(center.x + std::cos(a) * (rad + 2.5f), center.y + std::sin(a) * (rad + 2.5f));
    }
    dl->AddPolyline(bgArc, 25, C_METER_BG, ImDrawFlags_None, 2.0f);

    // Active green arc from center
    const float a0 = std::min(at, mid);
    const float a1 = std::max(at, mid);
    if (a1 - a0 > 0.02f) {
        ImVec2 actArc[17];
        for (int i = 0; i <= 16; ++i) {
            const float a = a0 + (a1 - a0) * (float)i / 16.0f;
            actArc[i] = ImVec2(center.x + std::cos(a) * (rad + 2.5f), center.y + std::sin(a) * (rad + 2.5f));
        }
        dl->AddPolyline(actArc, 17, C_LCD_TEXT, ImDrawFlags_None, 2.0f);
    }

    dl->AddCircleFilled(ImVec2(center.x, center.y + 1.0f), rad, IM_COL32(0, 0, 0, 100), 20);
    dl->AddCircleFilled(center, rad, IM_COL32(0x36, 0x36, 0x3a, 255), 20);
    dl->AddCircle(center, rad - 0.5f, IM_COL32(0x18, 0x18, 0x1a, 255), 20, 1.4f);

    const ImVec2 dir(std::cos(at), std::sin(at));
    dl->AddLine(ImVec2(center.x + dir.x * rad * 0.15f, center.y + dir.y * rad * 0.15f),
                ImVec2(center.x + dir.x * (rad - 2.0f), center.y + dir.y * (rad - 2.0f)),
                IM_COL32(0xee, 0xee, 0xf2, 255), 1.8f);
    return changed;
}

// Vertical peak meter (after widgets.rs meter & meter_scale)
void drawMeterBar(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float level, float hold, bool clip) {
    dl->AddRectFilled(ImVec2(mn.x, mn.y), ImVec2(mx.x, mn.y + 3.0f), clip ? C_METER_RED : IM_COL32(0x28, 0x16, 0x16, 255), 1.0f);
    const ImVec2 barMin(mn.x, mn.y + 4.0f);
    const ImVec2 barMax(mx.x, mx.y);
    dl->AddRectFilled(barMin, barMax, C_METER_BG);

    const float bh = std::max(1.0f, barMax.y - barMin.y);
    const auto yAtPos = [&](float pos) { return barMax.y - bh * std::clamp(pos, 0.0f, 1.0f); };
    const float amber = meterPos(-12.0f);
    const float red = meterPos(-3.0f);
    const float lit = levelPos(level);

    struct Zone { float lo; float hi; ImU32 col; ImU32 dimCol; };
    const Zone zones[3] = {
        {0.0f, amber, C_METER_GREEN,  IM_COL32(0x0c, 0x26, 0x12, 255)},
        {amber, red,  C_METER_YELLOW, IM_COL32(0x2a, 0x24, 0x0c, 255)},
        {red, 1.0f,   C_METER_RED,    IM_COL32(0x2a, 0x0e, 0x0e, 255)},
    };
    for (const auto& z : zones) {
        const float top = std::clamp(lit, z.lo, z.hi);
        if (top > z.lo) {
            dl->AddRectFilled(ImVec2(barMin.x + 1.0f, yAtPos(top)), ImVec2(barMax.x - 1.0f, yAtPos(z.lo)), z.col);
        }
        if (z.hi > top) {
            dl->AddRectFilled(ImVec2(barMin.x + 1.0f, yAtPos(z.hi)), ImVec2(barMax.x - 1.0f, yAtPos(top)), z.dimCol);
        }
    }

    static const float kMarks[] = {-5.0f, -10.0f, -15.0f, -20.0f, -30.0f, -40.0f, -60.0f};
    for (float m : kMarks) {
        const float y = std::floor(yAtPos(meterPos(m))) + 0.5f;
        dl->AddLine(ImVec2(barMin.x, y), ImVec2(barMax.x, y), IM_COL32(0, 0, 0, 100));
    }

    const float held = levelPos(hold);
    if (held > 0.01f) {
        const ImU32 hCol = held <= amber ? C_METER_GREEN : (held <= red ? C_METER_YELLOW : C_METER_RED);
        const float hy = yAtPos(held);
        dl->AddRectFilled(ImVec2(barMin.x + 1.0f, hy - 1.0f), ImVec2(barMax.x - 1.0f, hy + 1.0f), hCol);
    }
}

// Calibrated vertical fader (after widgets.rs fader)
std::optional<float> drawStripFader(const char* id, ImVec2 mn, ImVec2 mx, float db) {
    ImGui::SetCursorScreenPos(mn);
    ImGui::InvisibleButton(id, ImVec2(mx.x - mn.x, mx.y - mn.y));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    constexpr float kCapH = 36.0f;
    const float slotCx = (mn.x + mx.x) * 0.5f + 4.0f;
    const float slotTop = mn.y + kCapH * 0.5f;
    const float slotBot = mx.y - kCapH * 0.5f;
    const float slotH = std::max(20.0f, slotBot - slotTop);

    // Fader slot track
    dl->AddRectFilled(ImVec2(slotCx - 2.0f, slotTop), ImVec2(slotCx + 2.0f, slotBot), IM_COL32(0x10, 0x10, 0x12, 255), 2.0f);
    dl->AddLine(ImVec2(slotCx - 1.5f, slotTop + 1.0f), ImVec2(slotCx - 1.5f, slotBot - 1.0f), IM_COL32(0, 0, 0, 140));
    dl->AddLine(ImVec2(slotCx + 2.5f, slotTop + 1.0f), ImVec2(slotCx + 2.5f, slotBot - 1.0f), IM_COL32(255, 255, 255, 20));

    // Scale markings on the left of the fader slot
    const float capW = std::clamp((mx.x - mn.x) * 0.42f, 16.0f, 19.0f);
    const float numRightX = slotCx - capW * 0.5f - 2.0f;
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * 0.68f;
    float lastY = -1e9f;
    static const float kFaderMarks[] = {12.0f, 6.0f, 0.0f, -5.0f, -10.0f, -15.0f, -20.0f, -30.0f, -40.0f, -60.0f};
    for (float mark : kFaderMarks) {
        const float y = slotBot - slotH * Session::faderDbToPos(mark);
        dl->AddLine(ImVec2(slotCx - capW * 0.5f - 5.0f, y), ImVec2(slotCx - 3.0f, y), IM_COL32(150, 150, 150, 140));
        if (numRightX - mn.x >= 8.0f && y - lastY >= 9.0f) {
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "%.0f", std::abs(mark));
            const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, lbl);
            dl->AddText(font, fs, ImVec2(numRightX - ts.x - 3.0f, y - ts.y * 0.5f), C_DIM, lbl);
            lastY = y;
        }
    }

    // Fader cap
    const float pos = Session::faderDbToPos(db);
    const float capY = slotBot - slotH * pos;
    const ImVec2 capMin(slotCx - capW * 0.5f, capY - kCapH * 0.5f);
    const ImVec2 capMax(slotCx + capW * 0.5f, capY + kCapH * 0.5f);
    dl->AddRectFilled(ImVec2(capMin.x - 1.0f, capMin.y + 2.0f), ImVec2(capMax.x + 1.0f, capMax.y + 3.0f), IM_COL32(0, 0, 0, 90), 3.0f);
    dl->AddRectFilled(capMin, capMax, IM_COL32(34, 34, 36, 255), 3.0f);
    dl->AddRectFilled(ImVec2(capMin.x + 1.0f, capMin.y + 1.0f), ImVec2(capMax.x - 1.0f, capY), IM_COL32(196, 198, 204, 255), 2.0f);
    dl->AddRectFilled(ImVec2(capMin.x + 1.0f, capY), ImVec2(capMax.x - 1.0f, capMax.y - 1.0f), IM_COL32(148, 150, 156, 255), 2.0f);
    dl->AddLine(ImVec2(capMin.x + 2.0f, capMin.y + 1.5f), ImVec2(capMax.x - 2.0f, capMin.y + 1.5f), IM_COL32(255, 255, 255, 130));
    dl->AddLine(ImVec2(capMin.x + 1.0f, capY), ImVec2(capMax.x - 1.0f, capY), IM_COL32(25, 25, 28, 255), 2.0f);

    if ((hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
        (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::GetIO().KeyAlt)) {
        return 0.0f;
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const bool fine = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
        const float dy = ImGui::GetIO().MouseDelta.y / slotH * (fine ? 0.15f : 1.0f);
        const float np = std::clamp(pos - dy, 0.0f, 1.0f);
        return std::clamp(Session::faderPosToDb(np), -144.0f, 12.0f);
    }
    return std::nullopt;
}

// Draw EQ response curve inside a rectangle (after panels/plugins.rs eq_curve)
void drawEqCurveBox(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const std::string& pluginId,
                    const std::map<std::string, float>& paramMap, int numPts) {
    dl->AddRectFilled(mn, mx, IM_COL32(14, 18, 22, 255), 3.0f);
    dl->AddRect(mn, mx, C_BORDER, 3.0f);
    const float w = mx.x - mn.x;
    const float h = mx.y - mn.y;
    const float cy = (mn.y + mx.y) * 0.5f;
    for (float db : {-12.0f, 0.0f, 12.0f}) {
        const float y = cy - (db / 18.0f) * h * 0.45f;
        dl->AddLine(ImVec2(mn.x + 1.0f, y), ImVec2(mx.x - 1.0f, y), IM_COL32(40, 50, 60, 255));
    }
    std::vector<float> freqs(static_cast<size_t>(numPts));
    for (int i = 0; i < numPts; ++i) {
        freqs[static_cast<size_t>(i)] = 20.0f * std::pow(1000.0f, static_cast<float>(i) / static_cast<float>(numPts - 1));
    }
    std::vector<std::pair<std::string, float>> pvec(paramMap.begin(), paramMap.end());
    const std::vector<float> resp = (pluginId == "eq_7band")
        ? Dsp::eq7Response(pvec, freqs, 48000.0f)
        : Dsp::eq1Response(pvec, freqs, 48000.0f);

    std::vector<ImVec2> pts(resp.size());
    for (size_t i = 0; i < resp.size(); ++i) {
        const float px = mn.x + w * static_cast<float>(i) / static_cast<float>(resp.size() - 1);
        const float py = cy - (std::clamp(resp[i], -18.0f, 18.0f) / 18.0f) * h * 0.45f;
        pts[i] = ImVec2(px, py);
    }
    if (pts.size() >= 2) {
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), C_LCD_TEXT, ImDrawFlags_None, 1.8f);
    }
}

void drawInsertMenuPopup(Session::Document& doc, Session::Track& track, size_t slot) {
    const bool occupied = track.mixer.inserts[slot].has_value();
    if (occupied) {
        bool byp = track.mixer.inserts[slot]->bypass;
        if (ImGui::MenuItem(byp ? "Un-Bypass" : "Bypass")) {
            track.mixer.inserts[slot]->bypass = !byp;
            ++doc.touch;
        }
        if (ImGui::MenuItem("no insert")) {
            track.mixer.inserts[slot] = std::nullopt;
            syncInsertsSummary(track);
            ++doc.touch;
        }
        ImGui::Separator();
    }

    static const Dsp::Category kCats[] = {
        Dsp::Category::Eq, Dsp::Category::Dynamics, Dsp::Category::PitchShift, Dsp::Category::Reverb,
        Dsp::Category::Delay, Dsp::Category::Modulation, Dsp::Category::Harmonic, Dsp::Category::Dither,
        Dsp::Category::Other,
    };
    for (Dsp::Category cat : kCats) {
        if (ImGui::BeginMenu(Dsp::categoryLabel(cat))) {
            for (const Dsp::PluginInfo* p : Dsp::plugins()) {
                if (p->category == cat && !p->isInstrument) {
                    const bool isCur = occupied && track.mixer.inserts[slot]->plugin == p->id;
                    if (ImGui::MenuItem(p->name, nullptr, isCur)) {
                        track.mixer.inserts[slot] = Session::Insert::make(p->id);
                        syncInsertsSummary(track);
                        openPluginWindow(track.numId, slot);
                        ++doc.touch;
                    }
                }
            }
            ImGui::EndMenu();
        }
    }
}

void drawSendRoutePopup(Session::Document& doc, Session::Track& track, size_t slot) {
    const bool hasSend = track.mixer.sends[slot].has_value();
    const char letter = static_cast<char>('A' + slot);
    ImGui::Text("Send %c \xc2\xb7 Destination", letter);
    ImGui::Separator();
    for (const auto& bus : doc.busses) {
        const bool isCur = hasSend && track.mixer.sends[slot]->target.kind == Session::Route::Kind::Bus &&
                           track.mixer.sends[slot]->target.busId == bus.numId;
        if (ImGui::MenuItem(bus.name.c_str(), nullptr, isCur)) {
            if (!hasSend) {
                track.mixer.sends[slot] = Session::SendSlot::make(Session::Route::bus(bus.numId), 0.0f);
                gOpenSendWindow = std::make_pair(track.numId, slot);
            } else {
                track.mixer.sends[slot]->target = Session::Route::bus(bus.numId);
            }
            ++doc.touch;
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("new bus...")) {
        size_t n = doc.busses.size() + 1;
        while (Session::findBusByName(doc, "Bus " + std::to_string(n)) != nullptr) ++n;
        const uint64_t bid = Session::addBus(doc, "Bus " + std::to_string(n), Session::ChannelFormat::Stereo);
        track.mixer.sends[slot] = Session::SendSlot::make(Session::Route::bus(bid), 0.0f);
        gOpenSendWindow = std::make_pair(track.numId, slot);
        ++doc.touch;
    }
    if (hasSend && ImGui::MenuItem("Remove send")) {
        track.mixer.sends[slot] = std::nullopt;
        if (gOpenSendWindow == std::make_pair(track.numId, slot)) gOpenSendWindow = std::nullopt;
        ++doc.touch;
    }
}

} // namespace

// ============================================================================
// Public MixWindow Helpers & Unit-Tested Functions
// ============================================================================

float stripWidth(bool narrow) {
    return narrow ? 64.0f : 98.0f;
}

void setNarrowMix(bool narrow) { gNarrowMix = narrow; }
bool isNarrowMix() { return gNarrowMix; }
void toggleNarrowMix() { gNarrowMix = !gNarrowMix; }

// after SoundCraft (crates/ui-egui/src/mix_window/sends.rs parse_db)
std::optional<float> parseDb(const std::string& text) {
    std::string cleaned = toLowerAscii(trimAscii(text));
    const size_t dbPos = cleaned.find("db");
    if (dbPos != std::string::npos) cleaned.erase(dbPos, 2);
    for (char& ch : cleaned) {
        if (ch == ',') ch = '.';
    }
    cleaned = trimAscii(cleaned);
    if (cleaned == "-inf" || cleaned == "-infinity" || cleaned == "off") {
        return -144.0f;
    }
    if (cleaned.empty()) return std::nullopt;
    char* endPtr = nullptr;
    const float v = std::strtof(cleaned.c_str(), &endPtr);
    if (endPtr == cleaned.c_str() || *endPtr != '\0' || !std::isfinite(v)) {
        return std::nullopt;
    }
    return std::clamp(v, -144.0f, 12.0f);
}

// after SoundCraft (crates/ui-egui/src/mix_window/sends.rs parse_pan)
std::optional<float> parsePan(const std::string& text) {
    std::string cleaned = toLowerAscii(trimAscii(text));
    for (char& ch : cleaned) {
        if (ch == ',') ch = '.';
    }
    if (cleaned.empty()) return std::nullopt;
    float sign = 1.0f;
    std::string numStr = cleaned;
    if (cleaned.front() == '<' || cleaned.front() == 'l') {
        sign = -1.0f;
        numStr = trimAscii(cleaned.substr(1));
    } else if (cleaned.back() == '>') {
        sign = 1.0f;
        numStr = trimAscii(cleaned.substr(0, cleaned.size() - 1));
    } else if (cleaned.front() == 'r') {
        sign = 1.0f;
        numStr = trimAscii(cleaned.substr(1));
    }
    if (numStr.empty()) return std::nullopt;
    char* endPtr = nullptr;
    const float raw = std::strtof(numStr.c_str(), &endPtr) * sign;
    if (endPtr == numStr.c_str() || *endPtr != '\0' || !std::isfinite(raw)) {
        return std::nullopt;
    }
    const float val = (numStr.find('.') != std::string::npos && std::abs(raw) <= 1.0f) ? raw : (raw / 100.0f);
    if (!std::isfinite(val)) return std::nullopt;
    return std::clamp(val, -1.0f, 1.0f);
}

// after SoundCraft (crates/ui-egui/src/mix_window/sends.rs dragged_send_db)
float draggedSendDb(float db, float dy, bool fine) {
    const float perPoint = fine ? 0.0004f : 0.003f;
    return Session::faderPosToDb(Session::faderDbToPos(db) - dy * perPoint);
}

// after SoundCraft (crates/ui-egui/src/widgets.rs pan_text)
std::string panText(float v) {
    const int n = static_cast<int>(std::llround(v * 100.0f));
    if (n == 0) return "0";
    if (n < 0) return "<" + std::to_string(-n);
    return std::to_string(n) + ">";
}

// after SoundCraft (crates/ui-egui/src/widgets.rs db_text)
std::string dbText(float db) {
    if (db <= -143.9f) return "-inf";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", db);
    return buf;
}

// after SoundCraft (crates/ui-egui/src/widgets.rs meter_pos & level_pos)
float meterPos(float db) {
    if (!std::isfinite(db) || db <= -80.0f) return 0.0f;
    if (db >= 0.0f) return 1.0f;
    if (db < -40.0f) return (db + 80.0f) / 40.0f * 0.1f;
    if (db < -20.0f) return 0.1f + (db + 40.0f) / 20.0f * 0.2f;
    return 0.3f + (db + 20.0f) / 20.0f * 0.7f;
}

float levelPos(float v) {
    return meterPos(v <= 1e-6f ? -80.0f : 20.0f * std::log10(v));
}

// after SoundCraft (crates/ui-egui/src/mix_window/routing.rs route_name, input_name)
std::string routeName(const Session::Document& doc, const Session::Route& r) {
    switch (r.kind) {
        case Session::Route::Kind::None:
            return "no output";
        case Session::Route::Kind::Main:
            return "Out 1-2";
        case Session::Route::Kind::Bus: {
            const Session::Bus* b = Session::findBus(doc, r.busId);
            return b ? b->name : "bus?";
        }
        case Session::Route::Kind::Hardware:
            return r.hwName;
    }
    return "Out 1-2";
}

std::string inputName(const Session::Document& doc, const Session::Track& track) {
    if (Session::hasPlaylist(track.kind) && track.mixer.input.kind == Session::Route::Kind::None) {
        const size_t ch = Session::channelFormatChannels(track.format);
        return ch == 1 ? "In 1" : ("In 1-" + std::to_string(ch));
    }
    if (track.kind == Session::TrackKind::Master && track.mixer.input.kind == Session::Route::Kind::None) {
        return "Out 1-2";
    }
    return routeName(doc, track.mixer.input);
}

void openPluginWindow(uint64_t trackNumId, size_t slot) {
    const auto key = std::make_pair(trackNumId, slot);
    if (std::find(gOpenPluginWindows.begin(), gOpenPluginWindows.end(), key) == gOpenPluginWindows.end()) {
        gOpenPluginWindows.push_back(key);
    }
}

void closeAllPluginWindows() {
    gOpenPluginWindows.clear();
    gOpenSendWindow = std::nullopt;
}

void openSendWindow(uint64_t trackNumId, size_t slot) {
    gOpenSendWindow = std::make_pair(trackNumId, slot);
}

// ============================================================================
// Channel Strip Drawing (after SoundCraft crates/ui-egui/src/mix_window.rs)
// ============================================================================

static void drawChannelStrip(Session::Document& doc, int ti, float stripX, float stripY, float stripW, float stripH) {
    Session::Track& track = doc.tracks[(size_t)ti];
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool selected = std::find(doc.sessionEdit.selectedTracks.begin(), doc.sessionEdit.selectedTracks.end(), track.numId) != doc.sessionEdit.selectedTracks.end()
                       || Editor::selection().trackId == track.id;

    const ImVec2 rMin(stripX, stripY);
    const ImVec2 rMax(stripX + stripW, stripY + stripH);
    dl->AddRectFilled(rMin, rMax, selected ? C_STRIP_SEL : C_STRIP_BG);
    dl->AddLine(ImVec2(rMin.x, rMin.y), ImVec2(rMin.x, rMax.y), C_EDGE_LIGHT);
    dl->AddLine(ImVec2(rMax.x - 1.0f, rMin.y), ImVec2(rMax.x - 1.0f, rMax.y), C_BORDER);

    const float x0 = stripX + 4.0f;
    const float innerW = stripW - 8.0f;
    float y = stripY + 4.0f;
    ImFont* font = ImGui::GetFont();
    const float smallFs = ImGui::GetFontSize() * 0.78f;

    // 1. INSERTS A-E (5 slots)
    {
        const float secH = 16.0f + 5.0f * 17.0f;
        const ImVec2 secMin(x0, y);
        const ImVec2 secMax(x0 + innerW, y + secH);
        drawInsetBox(dl, secMin, secMax, C_SECTION);
        drawSectionHeader(dl, secMin, secMax, "INSERTS A-E", gNarrowMix);

        for (size_t k = 0; k < 5; ++k) {
            const ImVec2 sMin(x0 + 2.0f, secMin.y + 15.0f + k * 17.0f);
            const ImVec2 sMax(x0 + innerW - 2.0f, sMin.y + 15.0f);
            const auto& ins = track.mixer.inserts[k];

            char btnId[32], popId[32];
            std::snprintf(btnId, sizeof(btnId), "ins_btn_%d_%zu", ti, k);
            std::snprintf(popId, sizeof(popId), "ins_pop_%d_%zu", ti, k);
            ImGui::SetCursorScreenPos(sMin);
            const bool clicked = ImGui::InvisibleButton(btnId, ImVec2(sMax.x - sMin.x, sMax.y - sMin.y));
            const bool hovered = ImGui::IsItemHovered();

            if (!ins.has_value()) {
                dl->AddRectFilled(sMin, sMax, hovered ? C_SLOT_EMPTY_HI : C_SLOT_EMPTY, 2.0f);
                dl->AddCircleFilled(ImVec2(sMin.x + 5.0f, (sMin.y + sMax.y) * 0.5f), 1.8f, C_DIM);
                if (clicked) ImGui::OpenPopup(popId);
            } else {
                const Dsp::PluginInfo* inf = Dsp::pluginInfo(ins->plugin);
                const char* name = inf ? inf->shortName : ins->plugin.c_str();
                const ImU32 face = ins->bypass ? C_SLOT_BYP : C_SLOT_FACE;
                dl->AddRectFilled(sMin, sMax, face, 2.0f);
                dl->AddRect(sMin, sMax, C_BORDER, 2.0f);
                dl->AddLine(ImVec2(sMin.x + 1.0f, sMin.y + 1.0f), ImVec2(sMax.x - 1.0f, sMin.y + 1.0f), IM_COL32(255, 255, 255, 45));
                dl->AddCircleFilled(ImVec2(sMin.x + 5.0f, (sMin.y + sMax.y) * 0.5f), 1.8f, C_TEXT);
                dl->AddLine(ImVec2(sMin.x + 10.0f, sMin.y + 1.0f), ImVec2(sMin.x + 10.0f, sMax.y - 1.0f), IM_COL32(0, 0, 0, 90));
                const ImVec2 ts = font->CalcTextSizeA(smallFs, FLT_MAX, 0.0f, name);
                const float tx = std::max(sMin.x + 12.0f, (sMin.x + 10.0f + sMax.x - ts.x) * 0.5f);
                dl->AddText(font, smallFs, ImVec2(tx, (sMin.y + sMax.y - ts.y) * 0.5f), C_TEXT, name);

                if (clicked) {
                    if (ImGui::GetIO().KeyCtrl) {
                        track.mixer.inserts[k]->bypass = !track.mixer.inserts[k]->bypass;
                        ++doc.touch;
                    } else {
                        openPluginWindow(track.numId, k);
                    }
                }
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    ImGui::OpenPopup(popId);
                }
            }
            if (ImGui::BeginPopup(popId)) {
                drawInsertMenuPopup(doc, track, k);
                ImGui::EndPopup();
            }
        }
        y = secMax.y + 4.0f;
    }

    // 2. SENDS A-E (5 slots)
    {
        const float secH = 16.0f + 5.0f * 17.0f;
        const ImVec2 secMin(x0, y);
        const ImVec2 secMax(x0 + innerW, y + secH);
        drawInsetBox(dl, secMin, secMax, C_SECTION);
        drawSectionHeader(dl, secMin, secMax, "SENDS A-E", gNarrowMix);

        for (size_t k = 0; k < 5; ++k) {
            const ImVec2 sMin(x0 + 2.0f, secMin.y + 15.0f + k * 17.0f);
            const ImVec2 sMax(x0 + innerW - 2.0f, sMin.y + 15.0f);
            const auto& snd = track.mixer.sends[k];

            char btnId[32], popId[32];
            std::snprintf(btnId, sizeof(btnId), "snd_btn_%d_%zu", ti, k);
            std::snprintf(popId, sizeof(popId), "snd_pop_%d_%zu", ti, k);
            ImGui::SetCursorScreenPos(sMin);
            const bool clicked = ImGui::InvisibleButton(btnId, ImVec2(sMax.x - sMin.x, sMax.y - sMin.y));
            const bool hovered = ImGui::IsItemHovered();

            if (!snd.has_value()) {
                dl->AddRectFilled(sMin, sMax, hovered ? C_SLOT_EMPTY_HI : C_SLOT_EMPTY, 2.0f);
                dl->AddCircleFilled(ImVec2(sMin.x + 5.0f, (sMin.y + sMax.y) * 0.5f), 1.8f, C_DIM);
                if (clicked) ImGui::OpenPopup(popId);
            } else {
                const std::string rName = routeName(doc, snd->target);
                dl->AddRectFilled(sMin, sMax, C_SLOT_SEND, 2.0f);
                dl->AddRect(sMin, sMax, C_BORDER, 2.0f);
                dl->AddLine(ImVec2(sMin.x + 1.0f, sMin.y + 1.0f), ImVec2(sMax.x - 1.0f, sMin.y + 1.0f), IM_COL32(255, 255, 255, 45));
                dl->AddCircleFilled(ImVec2(sMin.x + 5.0f, (sMin.y + sMax.y) * 0.5f), 1.8f, C_TEXT);
                dl->AddLine(ImVec2(sMin.x + 10.0f, sMin.y + 1.0f), ImVec2(sMin.x + 10.0f, sMax.y - 1.0f), IM_COL32(0, 0, 0, 90));

                // Mini level indicator on right of send slot
                const float lvl = Session::faderDbToPos(snd->levelDb);
                const ImVec2 wMin(sMax.x - 8.0f, sMin.y + 2.0f);
                const ImVec2 wMax(sMax.x - 3.0f, sMax.y - 2.0f);
                dl->AddRectFilled(wMin, wMax, IM_COL32(0, 0, 0, 200), 1.0f);
                dl->AddRectFilled(ImVec2(wMin.x + 1.0f, wMax.y - (wMax.y - wMin.y - 2.0f) * lvl),
                                  ImVec2(wMax.x - 1.0f, wMax.y - 1.0f), C_LCD_TEXT);

                const ImVec2 ts = font->CalcTextSizeA(smallFs, FLT_MAX, 0.0f, rName.c_str());
                const float tx = std::max(sMin.x + 12.0f, (sMin.x + 10.0f + wMin.x - ts.x) * 0.5f);
                dl->AddText(font, smallFs, ImVec2(tx, (sMin.y + sMax.y - ts.y) * 0.5f), snd->mute ? C_MUTE : C_TEXT, rName.c_str());

                if (clicked) {
                    if (ImGui::GetIO().MousePos.x >= wMin.x - 4.0f) {
                        openSendWindow(track.numId, k);
                    } else {
                        ImGui::OpenPopup(popId);
                    }
                }
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    openSendWindow(track.numId, k);
                }
            }
            if (ImGui::BeginPopup(popId)) {
                drawSendRoutePopup(doc, track, k);
                ImGui::EndPopup();
            }
        }
        y = secMax.y + 4.0f;
    }

    // 3. I / O Section (Input, Output, Automation Mode)
    {
        const float secH = 72.0f;
        const ImVec2 secMin(x0, y);
        const ImVec2 secMax(x0 + innerW, y + secH);
        drawInsetBox(dl, secMin, secMax, C_SECTION);
        drawSectionHeader(dl, secMin, secMax, "I / O", gNarrowMix);

        auto drawSelectorRow = [&](const char* btnId, const char* popId, const std::string& text, float ry, ImU32 textCol) {
            const ImVec2 bMin(x0 + 2.0f, ry);
            const ImVec2 bMax(x0 + innerW - 2.0f, ry + 16.0f);
            ImGui::SetCursorScreenPos(bMin);
            const bool clicked = ImGui::InvisibleButton(btnId, ImVec2(bMax.x - bMin.x, 16.0f));
            const bool hov = ImGui::IsItemHovered();
            dl->AddRectFilled(bMin, bMax, hov ? IM_COL32(0x3a, 0x3a, 0x3e, 255) : IM_COL32(0x2e, 0x2e, 0x32, 255), 2.0f);
            dl->AddRect(bMin, bMax, C_BORDER, 2.0f);
            Icons::draw(dl, ImVec2(bMax.x - 10.0f, bMin.y + 5.0f), ImVec2(bMax.x - 4.0f, bMin.y + 11.0f), "triangle_down", C_DIM);
            const ImVec2 ts = font->CalcTextSizeA(smallFs, FLT_MAX, 0.0f, text.c_str());
            const float tx = std::max(bMin.x + 3.0f, (bMin.x + bMax.x - 10.0f - ts.x) * 0.5f);
            dl->PushClipRect(ImVec2(bMin.x + 2.0f, bMin.y), ImVec2(bMax.x - 11.0f, bMax.y), true);
            dl->AddText(font, smallFs, ImVec2(tx, (bMin.y + bMax.y - ts.y) * 0.5f), textCol, text.c_str());
            dl->PopClipRect();
            if (clicked) ImGui::OpenPopup(popId);
        };

        char inBtn[32], inPop[32], outBtn[32], outPop[32], amBtn[32], amPop[32];
        std::snprintf(inBtn, sizeof(inBtn), "io_in_%d", ti);
        std::snprintf(inPop, sizeof(inPop), "io_in_p_%d", ti);
        std::snprintf(outBtn, sizeof(outBtn), "io_out_%d", ti);
        std::snprintf(outPop, sizeof(outPop), "io_out_p_%d", ti);
        std::snprintf(amBtn, sizeof(amBtn), "io_am_%d", ti);
        std::snprintf(amPop, sizeof(amPop), "io_am_p_%d", ti);

        drawSelectorRow(inBtn, inPop, inputName(doc, track), secMin.y + 15.0f, C_TEXT);
        if (ImGui::BeginPopup(inPop)) {
            if (ImGui::MenuItem("no input")) track.mixer.input = Session::Route::none();
            for (int c = 1; c <= 8; ++c) {
                char inLbl[16];
                std::snprintf(inLbl, sizeof(inLbl), "In %d", c);
                if (ImGui::MenuItem(inLbl)) track.mixer.input = Session::Route::hardware(inLbl);
            }
            if (!doc.busses.empty()) {
                ImGui::Separator();
                for (const auto& b : doc.busses) {
                    if (ImGui::MenuItem(b.name.c_str())) track.mixer.input = Session::Route::bus(b.numId);
                }
            }
            ImGui::EndPopup();
        }

        drawSelectorRow(outBtn, outPop, routeName(doc, track.mixer.output), secMin.y + 33.0f, C_TEXT);
        if (ImGui::BeginPopup(outPop)) {
            if (ImGui::MenuItem("Out 1-2")) track.mixer.output = Session::Route::mainOut();
            for (const auto& b : doc.busses) {
                if (ImGui::MenuItem(b.name.c_str())) track.mixer.output = Session::Route::bus(b.numId);
            }
            ImGui::EndPopup();
        }

        const std::string amStr = track.automationMode.empty() ? "read" : track.automationMode;
        const std::string amLabel = gNarrowMix ? amStr : ("auto " + amStr);
        const ImU32 amCol = (amStr == "read") ? C_LCD_TEXT : (amStr == "off" ? C_DIM : C_REC);
        drawSelectorRow(amBtn, amPop, amLabel, secMin.y + 51.0f, amCol);
        if (ImGui::BeginPopup(amPop)) {
            for (const char* m : {"off", "read", "touch", "latch", "write"}) {
                if (ImGui::MenuItem(m, nullptr, amStr == m)) track.automationMode = m;
            }
            ImGui::EndPopup();
        }
        y = secMax.y + 4.0f;
    }

    // 4. Group Selector ("no group" or group name)
    {
        std::string gName = gNarrowMix ? "nogrp" : "no group";
        for (const auto& g : doc.groups) {
            if (std::find(g.members.begin(), g.members.end(), track.numId) != g.members.end()) {
                gName = g.name;
                break;
            }
        }
        const ImVec2 gMin(x0 + 2.0f, y);
        const ImVec2 gMax(x0 + innerW - 2.0f, y + 16.0f);
        dl->AddRectFilled(gMin, gMax, IM_COL32(0x2a, 0x2a, 0x2e, 255), 2.0f);
        dl->AddRect(gMin, gMax, C_BORDER, 2.0f);
        Icons::draw(dl, ImVec2(gMax.x - 10.0f, gMin.y + 5.0f), ImVec2(gMax.x - 4.0f, gMin.y + 11.0f), "triangle_down", C_DIM);
        const ImVec2 ts = font->CalcTextSizeA(smallFs, FLT_MAX, 0.0f, gName.c_str());
        dl->AddText(font, smallFs, ImVec2((gMin.x + gMax.x - 8.0f - ts.x) * 0.5f, (gMin.y + gMax.y - ts.y) * 0.5f), C_DIM, gName.c_str());
        y += 22.0f;
    }

    // 5. Pan Knobs & Console Readouts (<100 / 0 / 35>)
    if (track.kind != Session::TrackKind::Vca) {
        if (track.mixer.pan.empty()) {
            track.mixer.pan = (track.format == Session::ChannelFormat::Mono) ? std::vector<float>{track.pan} : std::vector<float>{-1.0f, 1.0f};
        }
        const size_t nPan = std::min<size_t>(2, track.mixer.pan.size());
        const float knobSz = (nPan == 2) ? std::min(30.0f, innerW * 0.5f - 6.0f) : 32.0f;
        for (size_t pi = 0; pi < nPan; ++pi) {
            const float cx = (nPan == 2) ? (x0 + innerW * (0.25f + 0.5f * (float)pi)) : (x0 + innerW * 0.5f);
            char kid[32];
            std::snprintf(kid, sizeof(kid), "pk_%d_%zu", ti, pi);
            if (drawPanKnob(kid, ImVec2(cx, y + 16.0f), knobSz, track.mixer.pan[pi], "Pan (drag; double-click centres)")) {
                if (pi == 0) track.pan = track.mixer.pan[0];
                ++doc.touch;
            }
            const std::string pTxt = panText(track.mixer.pan[pi]);
            const float rw = knobSz + 6.0f;
            const ImVec2 rMinP(cx - rw * 0.5f, y + 35.0f);
            const ImVec2 rMaxP(cx + rw * 0.5f, y + 48.0f);
            drawInsetBox(dl, rMinP, rMaxP, C_LCD);
            const ImVec2 ts = font->CalcTextSizeA(smallFs, FLT_MAX, 0.0f, pTxt.c_str());
            dl->AddText(font, smallFs, ImVec2((rMinP.x + rMaxP.x - ts.x) * 0.5f, (rMinP.y + rMaxP.y - ts.y) * 0.5f), C_LCD_TEXT, pTxt.c_str());
        }
        y += 52.0f;
    }

    // 6. I / R and S / M Toggle Rows
    {
        const float bw = (innerW - 6.0f) * 0.5f;
        auto drawToggleBtn = [&](const char* id, ImVec2 pos, ImVec2 sz, const char* lbl, bool on, ImU32 onCol, bool isRecDot) {
            ImGui::SetCursorScreenPos(pos);
            const bool clicked = ImGui::InvisibleButton(id, sz);
            const bool hov = ImGui::IsItemHovered();
            const ImVec2 br(pos.x + sz.x, pos.y + sz.y);
            const ImU32 bg = on ? onCol : (hov ? IM_COL32(0x42, 0x42, 0x46, 255) : IM_COL32(0x33, 0x33, 0x37, 255));
            dl->AddRectFilled(pos, br, bg, 2.5f);
            dl->AddRect(pos, br, C_BORDER, 2.5f);
            if (isRecDot) {
                dl->AddCircleFilled(ImVec2((pos.x + br.x) * 0.5f, (pos.y + br.y) * 0.5f), sz.y * 0.24f,
                                    on ? IM_COL32(255, 255, 255, 255) : IM_COL32(214, 214, 214, 255));
            } else {
                const ImVec2 ts = ImGui::CalcTextSize(lbl);
                dl->AddText(ImVec2((pos.x + br.x - ts.x) * 0.5f, (pos.y + br.y - ts.y) * 0.5f),
                            on ? IM_COL32(15, 15, 15, 255) : C_TEXT, lbl);
            }
            return clicked;
        };

        if (Session::hasPlaylist(track.kind)) {
            char iId[32], rId[32];
            std::snprintf(iId, sizeof(iId), "mx_i_%d", ti);
            std::snprintf(rId, sizeof(rId), "mx_r_%d", ti);
            if (drawToggleBtn(iId, ImVec2(x0, y), ImVec2(bw, 18.0f), "I", track.inputMonitor, C_METER_GREEN, false)) {
                track.inputMonitor = !track.inputMonitor;
            }
            if (drawToggleBtn(rId, ImVec2(x0 + bw + 6.0f, y), ImVec2(bw, 18.0f), "R", track.recordArm, C_REC, true)) {
                track.recordArm = !track.recordArm;
            }
        }
        y += 22.0f;

        if (track.kind != Session::TrackKind::Master) {
            char sId[32], mId[32];
            std::snprintf(sId, sizeof(sId), "mx_s_%d", ti);
            std::snprintf(mId, sizeof(mId), "mx_m_%d", ti);
            if (drawToggleBtn(sId, ImVec2(x0, y), ImVec2(bw, 18.0f), "S", track.solo, C_SOLO, false)) {
                track.solo = !track.solo;
                ++doc.touch;
            }
            if (drawToggleBtn(mId, ImVec2(x0 + bw + 6.0f, y), ImVec2(bw, 18.0f), "M", track.mute, C_MUTE, false)) {
                track.mute = !track.mute;
                ++doc.touch;
            }
        }
        y += 24.0f;
    }

    // 7. Fader + Peak Meter
    constexpr float kFootH = 60.0f;
    {
        const float fh = std::max(120.0f, rMax.y - kFootH - y);
        const ImVec2 faderMin(x0, y);
        const ImVec2 faderMax(x0 + innerW * 0.60f, y + fh);
        char fid[32];
        std::snprintf(fid, sizeof(fid), "mx_fdr_%d", ti);
        if (auto newDb = drawStripFader(fid, faderMin, faderMax, track.gainDb)) {
            track.gainDb = *newDb;
            ++doc.touch;
        }

        const ImVec2 meterMin(faderMax.x + 4.0f, y + 13.0f);
        const ImVec2 meterMax(x0 + innerW - 2.0f, y + fh - 17.0f);
        float simPeak = 0.0f;
        if (Playback::isPlaying() && !track.mute && !track.inactive) {
            simPeak = std::clamp(Dsp::dbToGain(track.gainDb) * 0.72f, 0.0f, 1.0f);
        }
        const size_t nCh = (track.kind == Session::TrackKind::Master) ? 2 : std::min<size_t>(2, Session::channelFormatChannels(track.format));
        if (nCh >= 2) {
            const float mw = (meterMax.x - meterMin.x - 2.0f) * 0.5f;
            drawMeterBar(dl, meterMin, ImVec2(meterMin.x + mw, meterMax.y), simPeak, simPeak, false);
            drawMeterBar(dl, ImVec2(meterMin.x + mw + 2.0f, meterMin.y), meterMax, simPeak * 0.94f, simPeak * 0.94f, false);
        } else {
            drawMeterBar(dl, meterMin, meterMax, simPeak, simPeak, false);
        }
    }

    // 8. Footer: Volume dB Readout, Track Kind label, Name Plate, and Colour Bar
    {
        const ImVec2 vrMin(x0 + 4.0f, rMax.y - kFootH + 4.0f);
        const ImVec2 vrMax(x0 + innerW - 4.0f, vrMin.y + 15.0f);
        drawInsetBox(dl, vrMin, vrMax, C_LCD);
        const std::string vTxt = dbText(track.gainDb);
        const ImVec2 vTs = font->CalcTextSizeA(smallFs, FLT_MAX, 0.0f, vTxt.c_str());
        dl->AddText(font, smallFs, ImVec2((vrMin.x + vrMax.x - vTs.x) * 0.5f, (vrMin.y + vrMax.y - vTs.y) * 0.5f), C_LCD_TEXT, vTxt.c_str());

        const char* kindStr = "audio";
        switch (track.kind) {
            case Session::TrackKind::Audio: kindStr = "audio"; break;
            case Session::TrackKind::Aux: kindStr = "aux"; break;
            case Session::TrackKind::Master: kindStr = "master"; break;
            case Session::TrackKind::Midi: kindStr = "midi"; break;
            case Session::TrackKind::Instrument: kindStr = "inst"; break;
            case Session::TrackKind::Vca: kindStr = "vca"; break;
            case Session::TrackKind::Folder: kindStr = "folder"; break;
            case Session::TrackKind::Video: kindStr = "video"; break;
        }
        const ImVec2 kTs = font->CalcTextSizeA(smallFs, FLT_MAX, 0.0f, kindStr);
        dl->AddText(font, smallFs, ImVec2((vrMin.x + vrMax.x - kTs.x) * 0.5f, vrMax.y + 2.0f), C_DIM, kindStr);

        // High-contrast Track Name Plate (as in Pro Tools / SoundCraft mix.png)
        const ImVec2 npMin(x0, rMax.y - 25.0f);
        const ImVec2 npMax(x0 + innerW, rMax.y - 7.0f);
        dl->AddRectFilled(npMin, npMax, selected ? C_NAME_PLATE_SEL : C_NAME_PLATE, 2.0f);
        dl->AddRect(npMin, npMax, C_BORDER, 2.0f);
        const ImVec2 nTs = ImGui::CalcTextSize(track.name.c_str());
        dl->PushClipRect(ImVec2(npMin.x + 2.0f, npMin.y), ImVec2(npMax.x - 2.0f, npMax.y), true);
        dl->AddText(ImVec2((npMin.x + npMax.x - nTs.x) * 0.5f, (npMin.y + npMax.y - nTs.y) * 0.5f), C_NAME_TEXT, track.name.c_str());
        dl->PopClipRect();

        ImGui::SetCursorScreenPos(npMin);
        char npId[32];
        std::snprintf(npId, sizeof(npId), "mx_np_%d", ti);
        if (ImGui::InvisibleButton(npId, ImVec2(innerW, 18.0f))) {
            if (!ImGui::GetIO().KeyShift) doc.sessionEdit.selectedTracks.clear();
            doc.sessionEdit.selectedTracks.push_back(track.numId);
            Editor::selectRange(Editor::selection().start, Editor::selection().end, track.id);
        }

        // Bottom 5px Track Colour Bar
        const ImU32 tCol = IM_COL32(track.color[0], track.color[1], track.color[2], 255);
        dl->AddRectFilled(ImVec2(rMin.x, rMax.y - 5.0f), ImVec2(rMax.x - 1.0f, rMax.y), tCol);
    }
}

void draw(Session::Document& doc) {
    ImGui::BeginChild("mix_strips_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float stripH = std::max(580.0f, avail.y - 4.0f);
    const float w = stripWidth(gNarrowMix);

    int visIdx = 0;
    for (int ti = 0; ti < (int)doc.tracks.size(); ++ti) {
        if (doc.tracks[(size_t)ti].hidden || doc.tracks[(size_t)ti].kind == Session::TrackKind::Video) continue;
        const float sx = p0.x + visIdx * (w + 1.0f);
        drawChannelStrip(doc, ti, sx, p0.y, w, stripH);
        ++visIdx;
    }
    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(ImVec2(visIdx * (w + 1.0f) + 16.0f, stripH));
    ImGui::EndChild();
}

// ============================================================================
// Floating Plugin Editor & Send Windows (after panels/plugins.rs & mix_window/sends.rs)
// ============================================================================

void drawFloatingWindows(Session::Document& doc) {
    // 1. Floating Plugin Editor Windows
    std::vector<std::pair<uint64_t, size_t>> keep;
    for (const auto& [tid, slot] : gOpenPluginWindows) {
        Session::Track* track = Session::findTrackByNum(doc, tid);
        if (!track) continue;
        const bool isInst = (slot == kInstrumentSlot);
        Session::Insert* ins = isInst
            ? (track->instrument.has_value() ? &(*track->instrument) : nullptr)
            : (slot < Session::kInsertSlots && track->mixer.inserts[slot].has_value() ? &(*track->mixer.inserts[slot]) : nullptr);
        if (!ins) continue;
        const Dsp::PluginInfo* info = Dsp::pluginInfo(ins->plugin);
        if (!info) continue;

        const std::string place = isInst ? "instrument" : std::string(1, static_cast<char>('a' + slot));
        char winTitle[256];
        std::snprintf(winTitle, sizeof(winTitle), "%s \xc2\xb7 %s \xc2\xb7 %s###plug_%llu_%zu",
                      track->name.c_str(), place.c_str(), info->name,
                      static_cast<unsigned long long>(tid), slot);

        bool open = true;
        const ImVec2 vpSize = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowSize(ImVec2(370.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(vpSize.x - 395.0f, 92.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(winTitle, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!isInst) {
                ImGui::TextDisabled("%s", ins->preset.c_str());
                ImGui::SameLine(0.0f, 10.0f);
                if (ins->bypass) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(C_BLUE));
                if (ImGui::SmallButton(ins->bypass ? "BYPASSED" : "Bypass")) {
                    ins->bypass = !ins->bypass;
                    ++doc.touch;
                }
                if (ins->bypass) ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 8.0f);
                if (ImGui::SmallButton("Reset")) {
                    ins->params.clear();
                    ins->preset = "<factory default>";
                    ++doc.touch;
                }
            }

            if ( std::strcmp(info->id, "eq_7band") == 0 || std::strcmp(info->id, "eq_1band") == 0) {
                ImGui::Spacing();
                const ImVec2 cMin = ImGui::GetCursorScreenPos();
                const ImVec2 cSz(350.0f, 96.0f);
                drawEqCurveBox(ImGui::GetWindowDrawList(), cMin, ImVec2(cMin.x + cSz.x, cMin.y + cSz.y), info->id, ins->params, 140);
                ImGui::Dummy(cSz);
                ImGui::Spacing();
            }

            if (ImGui::BeginTable("plug_params_grid", 3, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Param", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 168.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 82.0f);

                for (size_t pi = 0; pi < info->params.size(); ++pi) {
                    const auto& p = info->params[pi];
                    ImGui::PushID(static_cast<int>(pi));
                    ImGui::TableNextRow();

                    const float curVal = ins->params.count(p.id) ? ins->params.at(p.id) : p.defaultVal;
                    float x = curVal;

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(p.name);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(162.0f);
                    if (!p.choices.empty()) {
                        int idx = std::clamp(static_cast<int>(std::llround(x)), 0, static_cast<int>(p.choices.size()) - 1);
                        if (ImGui::Combo("##choice", &idx, p.choices.data(), static_cast<int>(p.choices.size()))) {
                            x = static_cast<float>(idx);
                        }
                    } else {
                        const ImGuiSliderFlags flags = (p.taper == Dsp::Taper::Log && p.minVal > 0.0f) ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None;
                        ImGui::SliderFloat("##slider", &x, p.minVal, p.maxVal, "", flags);
                    }

                    if (std::abs(x - curVal) > 1e-5f) {
                        ins->params[p.id] = p.clamp(x);
                        ++doc.touch;
                    }

                    ImGui::TableSetColumnIndex(2);
                    const std::string valStr = p.format(ins->params.count(p.id) ? ins->params.at(p.id) : p.defaultVal);
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_DIM), "%s", valStr.c_str());

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
        if (open) keep.push_back({tid, slot});
    }
    gOpenPluginWindows = std::move(keep);

    // 2. Floating Send Window
    if (gOpenSendWindow.has_value()) {
        const auto [tid, slot] = *gOpenSendWindow;
        Session::Track* track = Session::findTrackByNum(doc, tid);
        if (!track || slot >= Session::kSendSlots || !track->mixer.sends[slot].has_value()) {
            gOpenSendWindow = std::nullopt;
            return;
        }
        Session::SendSlot& snd = *track->mixer.sends[slot];
        char winTitle[128];
        std::snprintf(winTitle, sizeof(winTitle), "%s \xc2\xb7 Send %c###send_win", track->name.c_str(), static_cast<char>('A' + slot));
        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(160.0f, 0.0f), ImGuiCond_Always);
        if (ImGui::Begin(winTitle, &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            const std::string rName = routeName(doc, snd.target);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_LCD_TEXT), "%s", rName.c_str());
            if (ImGui::Checkbox("Pre-Fader", &snd.preFader)) ++doc.touch;
            if (ImGui::Checkbox("Mute", &snd.mute)) ++doc.touch;
            if (ImGui::SliderFloat("Pan", &snd.pan, -1.0f, 1.0f, "%.2f")) ++doc.touch;
            if (ImGui::SliderFloat("Level", &snd.levelDb, -60.0f, 12.0f, "%.1f dB")) ++doc.touch;
            if (ImGui::SmallButton("0 dB")) {
                snd.levelDb = 0.0f;
                ++doc.touch;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("-inf")) {
                snd.levelDb = -144.0f;
                ++doc.touch;
            }
        }
        ImGui::End();
        if (!open) gOpenSendWindow = std::nullopt;
    }
}

} // namespace MixWindow
