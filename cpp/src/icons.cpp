// Polyhymnia - procedural icons ported 1-to-1 from SoundCraft (after SoundCraft crates/ui-egui/src/icons.rs & toolbar.rs).
#include "icons.h"
#include "time_format.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Icons {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

// after SoundCraft (crates/ui-egui/src/icons.rs p)
ImVec2 p(const ImVec2& rMin, const ImVec2& rMax, float x, float y) {
    const float w = rMax.x - rMin.x;
    const float h = rMax.y - rMin.y;
    return ImVec2(rMin.x + w * x, rMin.y + h * y);
}

ImVec2 center(const ImVec2& rMin, const ImVec2& rMax) {
    return ImVec2((rMin.x + rMax.x) * 0.5f, (rMin.y + rMax.y) * 0.5f);
}

} // namespace

// after SoundCraft (crates/ui-egui/src/icons.rs draw)
void draw(ImDrawList* dl, const ImVec2& rMin, const ImVec2& rMax, const char* name, ImU32 c) {
    if (!dl || !name) return;
    const float rw = rMax.x - rMin.x;
    const float rh = rMax.y - rMin.y;
    const float sw = std::clamp(rw / 14.0f, 1.2f, 2.4f);

    if (std::strcmp(name, "play") == 0) {
        const ImVec2 pts[3] = {p(rMin, rMax, 0.28f, 0.18f), p(rMin, rMax, 0.82f, 0.5f), p(rMin, rMax, 0.28f, 0.82f)};
        dl->AddConvexPolyFilled(pts, 3, c);
        return;
    }
    if (std::strcmp(name, "stop") == 0) {
        dl->AddRectFilled(p(rMin, rMax, 0.26f, 0.26f), p(rMin, rMax, 0.74f, 0.74f), c, 1.0f);
        return;
    }
    if (std::strcmp(name, "record") == 0) {
        dl->AddCircleFilled(center(rMin, rMax), rw * 0.27f, c, 20);
        return;
    }
    if (std::strcmp(name, "rtz") == 0) {
        dl->AddRectFilled(p(rMin, rMax, 0.24f, 0.26f), p(rMin, rMax, 0.34f, 0.74f), c, 0.0f);
        const ImVec2 pts[3] = {p(rMin, rMax, 0.76f, 0.26f), p(rMin, rMax, 0.76f, 0.74f), p(rMin, rMax, 0.36f, 0.5f)};
        dl->AddConvexPolyFilled(pts, 3, c);
        return;
    }
    if (std::strcmp(name, "end") == 0) {
        dl->AddRectFilled(p(rMin, rMax, 0.66f, 0.26f), p(rMin, rMax, 0.76f, 0.74f), c, 0.0f);
        const ImVec2 pts[3] = {p(rMin, rMax, 0.24f, 0.26f), p(rMin, rMax, 0.64f, 0.5f), p(rMin, rMax, 0.24f, 0.74f)};
        dl->AddConvexPolyFilled(pts, 3, c);
        return;
    }
    if (std::strcmp(name, "rewind") == 0) {
        for (float dx : {0.0f, 0.26f}) {
            const ImVec2 pts[3] = {p(rMin, rMax, 0.5f + dx, 0.28f), p(rMin, rMax, 0.5f + dx, 0.72f), p(rMin, rMax, 0.22f + dx, 0.5f)};
            dl->AddConvexPolyFilled(pts, 3, c);
        }
        return;
    }
    if (std::strcmp(name, "ffwd") == 0) {
        for (float dx : {0.0f, 0.26f}) {
            const ImVec2 pts[3] = {p(rMin, rMax, 0.24f + dx, 0.28f), p(rMin, rMax, 0.52f + dx, 0.5f), p(rMin, rMax, 0.24f + dx, 0.72f)};
            dl->AddConvexPolyFilled(pts, 3, c);
        }
        return;
    }
    if (std::strcmp(name, "loop") == 0) {
        const ImVec2 rrMin = p(rMin, rMax, 0.2f, 0.3f);
        const ImVec2 rrMax = p(rMin, rMax, 0.8f, 0.7f);
        dl->AddRect(rrMin, rrMax, c, (rrMax.y - rrMin.y) * 0.5f, 0, sw);
        const ImVec2 pts[3] = {p(rMin, rMax, 0.55f, 0.2f), p(rMin, rMax, 0.68f, 0.3f), p(rMin, rMax, 0.55f, 0.4f)};
        dl->AddConvexPolyFilled(pts, 3, c);
        return;
    }
    if (std::strcmp(name, "zoom") == 0) {
        dl->AddCircle(p(rMin, rMax, 0.42f, 0.4f), rw * 0.2f, c, 20, sw);
        dl->AddLine(p(rMin, rMax, 0.56f, 0.55f), p(rMin, rMax, 0.8f, 0.8f), c, sw * 1.6f);
        return;
    }
    if (std::strcmp(name, "zoom_toggle") == 0) {
        const float corners[4][4] = {
            {0.18f, 0.24f, 0.16f, 0.14f},
            {0.82f, 0.24f, -0.16f, 0.14f},
            {0.18f, 0.76f, 0.16f, -0.14f},
            {0.82f, 0.76f, -0.16f, -0.14f},
        };
        for (const auto& cr : corners) {
            const ImVec2 pts[3] = {p(rMin, rMax, cr[0] + cr[2], cr[1]), p(rMin, rMax, cr[0], cr[1]), p(rMin, rMax, cr[0], cr[1] + cr[3])};
            dl->AddPolyline(pts, 3, c, ImDrawFlags_None, sw);
        }
        dl->AddRectFilled(p(rMin, rMax, 0.34f, 0.41f), p(rMin, rMax, 0.66f, 0.59f), c, 1.0f);
        return;
    }
    if (std::strcmp(name, "trim") == 0) {
        dl->AddLine(p(rMin, rMax, 0.5f, 0.2f), p(rMin, rMax, 0.5f, 0.8f), c, sw);
        dl->AddLine(p(rMin, rMax, 0.15f, 0.5f), p(rMin, rMax, 0.85f, 0.5f), c, sw);
        for (float side : {0.0f, 1.0f}) {
            const float a = side == 0.0f ? 0.15f : 0.85f;
            const float b = 0.5f;
            const float d = side == 0.0f ? 1.0f : -1.0f;
            const ImVec2 pts[3] = {p(rMin, rMax, a, b), p(rMin, rMax, a + 0.12f * d, b - 0.1f), p(rMin, rMax, a + 0.12f * d, b + 0.1f)};
            dl->AddConvexPolyFilled(pts, 3, c);
        }
        return;
    }
    if (std::strcmp(name, "selector") == 0) {
        dl->AddLine(p(rMin, rMax, 0.5f, 0.2f), p(rMin, rMax, 0.5f, 0.8f), c, sw);
        dl->AddLine(p(rMin, rMax, 0.4f, 0.2f), p(rMin, rMax, 0.6f, 0.2f), c, sw);
        dl->AddLine(p(rMin, rMax, 0.4f, 0.8f), p(rMin, rMax, 0.6f, 0.8f), c, sw);
        const float hs[5] = {0.1f, 0.22f, 0.14f, 0.26f, 0.12f};
        for (int i = 0; i < 5; ++i) {
            const float h = hs[i];
            const float x = 0.12f + static_cast<float>(i) * 0.06f;
            dl->AddLine(p(rMin, rMax, x, 0.5f - h), p(rMin, rMax, x, 0.5f + h), c, sw * 0.7f);
            const float x2 = 0.88f - static_cast<float>(i) * 0.06f;
            dl->AddLine(p(rMin, rMax, x2, 0.5f - h), p(rMin, rMax, x2, 0.5f + h), c, sw * 0.7f);
        }
        return;
    }
    if (std::strcmp(name, "grabber") == 0) {
        dl->AddRect(p(rMin, rMax, 0.3f, 0.45f), p(rMin, rMax, 0.72f, 0.82f), c, 4.0f, 0, sw);
        const float tops[4] = {0.22f, 0.16f, 0.18f, 0.26f};
        for (int i = 0; i < 4; ++i) {
            const float x = 0.34f + static_cast<float>(i) * 0.11f;
            dl->AddLine(p(rMin, rMax, x, 0.47f), p(rMin, rMax, x, tops[i]), c, sw);
        }
        dl->AddLine(p(rMin, rMax, 0.3f, 0.62f), p(rMin, rMax, 0.18f, 0.48f), c, sw);
        return;
    }
    if (std::strcmp(name, "scrubber") == 0) {
        const ImVec2 pts[6] = {
            p(rMin, rMax, 0.18f, 0.4f),  p(rMin, rMax, 0.3f, 0.4f),  p(rMin, rMax, 0.46f, 0.24f),
            p(rMin, rMax, 0.46f, 0.76f), p(rMin, rMax, 0.3f, 0.6f),  p(rMin, rMax, 0.18f, 0.6f),
        };
        dl->AddConvexPolyFilled(pts, 6, c);
        const float rads[2] = {0.14f, 0.26f};
        const ImVec2 cc = p(rMin, rMax, 0.46f, 0.5f);
        for (int k = 0; k < 2; ++k) {
            ImVec2 arc[9];
            for (int i = 0; i <= 8; ++i) {
                const float a = -0.8f + 1.6f * static_cast<float>(i) / 8.0f;
                arc[i] = ImVec2(cc.x + std::cos(a) * rw * rads[k], cc.y + std::sin(a) * rh * rads[k]);
            }
            dl->AddPolyline(arc, 9, c, ImDrawFlags_None, sw * (1.0f - static_cast<float>(k) * 0.2f));
        }
        return;
    }
    if (std::strcmp(name, "pencil") == 0) {
        dl->AddLine(p(rMin, rMax, 0.25f, 0.75f), p(rMin, rMax, 0.72f, 0.28f), c, sw * 2.2f);
        const ImVec2 pts[3] = {p(rMin, rMax, 0.18f, 0.82f), p(rMin, rMax, 0.22f, 0.66f), p(rMin, rMax, 0.34f, 0.78f)};
        dl->AddConvexPolyFilled(pts, 3, c);
        return;
    }
    if (std::strcmp(name, "speaker") == 0) {
        const ImVec2 pts[6] = {
            p(rMin, rMax, 0.2f, 0.4f),   p(rMin, rMax, 0.34f, 0.4f),  p(rMin, rMax, 0.52f, 0.22f),
            p(rMin, rMax, 0.52f, 0.78f), p(rMin, rMax, 0.34f, 0.6f),  p(rMin, rMax, 0.2f, 0.6f),
        };
        dl->AddConvexPolyFilled(pts, 6, c);
        return;
    }
    if (std::strcmp(name, "click") == 0) {
        const ImVec2 pts[3] = {p(rMin, rMax, 0.5f, 0.15f), p(rMin, rMax, 0.78f, 0.85f), p(rMin, rMax, 0.22f, 0.85f)};
        dl->AddPolyline(pts, 3, c, ImDrawFlags_Closed, sw);
        dl->AddLine(p(rMin, rMax, 0.5f, 0.7f), p(rMin, rMax, 0.7f, 0.25f), c, sw);
        return;
    }
    if (std::strcmp(name, "link") == 0) {
        dl->AddCircle(p(rMin, rMax, 0.38f, 0.5f), rw * 0.16f, c, 16, sw);
        dl->AddCircle(p(rMin, rMax, 0.62f, 0.5f), rw * 0.16f, c, 16, sw);
        return;
    }
    if (std::strcmp(name, "plus") == 0) {
        dl->AddLine(p(rMin, rMax, 0.5f, 0.25f), p(rMin, rMax, 0.5f, 0.75f), c, sw);
        dl->AddLine(p(rMin, rMax, 0.25f, 0.5f), p(rMin, rMax, 0.75f, 0.5f), c, sw);
        return;
    }
    if (std::strcmp(name, "minus") == 0) {
        dl->AddLine(p(rMin, rMax, 0.25f, 0.5f), p(rMin, rMax, 0.75f, 0.5f), c, sw);
        return;
    }
    if (std::strcmp(name, "triangle_down") == 0) {
        const ImVec2 pts[3] = {p(rMin, rMax, 0.25f, 0.35f), p(rMin, rMax, 0.75f, 0.35f), p(rMin, rMax, 0.5f, 0.68f)};
        dl->AddConvexPolyFilled(pts, 3, c);
        return;
    }
    if (std::strcmp(name, "note") == 0) {
        dl->AddCircleFilled(p(rMin, rMax, 0.38f, 0.72f), rw * 0.13f, c, 12);
        dl->AddLine(p(rMin, rMax, 0.5f, 0.72f), p(rMin, rMax, 0.5f, 0.2f), c, sw);
        dl->AddLine(p(rMin, rMax, 0.5f, 0.2f), p(rMin, rMax, 0.72f, 0.32f), c, sw);
        return;
    }
    if (std::strcmp(name, "gear") == 0) {
        const ImVec2 cc = center(rMin, rMax);
        dl->AddCircle(cc, rw * 0.2f, c, 16, sw);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * kTau / 8.0f;
            const ImVec2 c0(cc.x + std::cos(a) * rw * 0.22f, cc.y + std::sin(a) * rw * 0.22f);
            const ImVec2 c1(cc.x + std::cos(a) * rw * 0.34f, cc.y + std::sin(a) * rw * 0.34f);
            dl->AddLine(c0, c1, c, sw * 1.4f);
        }
        return;
    }
    if (std::strcmp(name, "close") == 0) {
        dl->AddLine(p(rMin, rMax, 0.3f, 0.3f), p(rMin, rMax, 0.7f, 0.7f), c, sw);
        dl->AddLine(p(rMin, rMax, 0.7f, 0.3f), p(rMin, rMax, 0.3f, 0.7f), c, sw);
        return;
    }
    if (std::strcmp(name, "lock") == 0) {
        dl->AddRectFilled(p(rMin, rMax, 0.22f, 0.46f), p(rMin, rMax, 0.78f, 0.88f), c, rw * 0.08f);
        const float cx = (rMin.x + rMax.x) * 0.5f;
        const float cy = rMin.y + rh * 0.46f;
        const float w = rw * 0.19f;
        ImVec2 arc[13];
        for (int i = 0; i <= 12; ++i) {
            const float a = kPi * static_cast<float>(i) / 12.0f;
            arc[i] = ImVec2(cx - w * std::cos(a), cy - w * 1.4f * std::sin(a));
        }
        dl->AddPolyline(arc, 13, c, ImDrawFlags_None, sw);
        return;
    }
    if (std::strcmp(name, "wave") == 0) {
        const float midY = (rMin.y + rMax.y) * 0.5f;
        ImVec2 pts[25];
        for (int i = 0; i <= 24; ++i) {
            const float t = static_cast<float>(i) / 24.0f;
            const float env = std::max(0.1f, 1.0f - std::abs(t - 0.5f) * 1.6f);
            pts[i] = ImVec2(rMin.x + rw * (0.12f + 0.76f * t), midY - std::sin(t * 18.0f) * rh * 0.28f * env);
        }
        dl->AddPolyline(pts, 25, c, ImDrawFlags_None, sw);
        return;
    }
    if (std::strcmp(name, "tab_transient") == 0) {
        dl->AddLine(p(rMin, rMax, 0.08f, 0.5f), p(rMin, rMax, 0.3f, 0.5f), c, sw);
        const ImVec2 pts[3] = {p(rMin, rMax, 0.42f, 0.5f), p(rMin, rMax, 0.28f, 0.36f), p(rMin, rMax, 0.28f, 0.64f)};
        dl->AddConvexPolyFilled(pts, 3, c);
        dl->AddLine(p(rMin, rMax, 0.5f, 0.16f), p(rMin, rMax, 0.5f, 0.84f), c, sw);
        const float spikes[3][2] = {{0.64f, 0.32f}, {0.78f, 0.2f}, {0.92f, 0.1f}};
        for (const auto& sp : spikes) {
            dl->AddLine(p(rMin, rMax, sp[0], 0.5f - sp[1]), p(rMin, rMax, sp[0], 0.5f + sp[1]), c, sw);
        }
        return;
    }
    if (std::strcmp(name, "link_track") == 0) {
        dl->AddLine(p(rMin, rMax, 0.12f, 0.18f), p(rMin, rMax, 0.88f, 0.18f), c, sw);
        dl->AddLine(p(rMin, rMax, 0.12f, 0.32f), p(rMin, rMax, 0.88f, 0.32f), c, sw);
        dl->AddCircle(p(rMin, rMax, 0.38f, 0.72f), rw * 0.16f, c, 16, sw);
        dl->AddCircle(p(rMin, rMax, 0.62f, 0.72f), rw * 0.16f, c, 16, sw);
        return;
    }
    if (std::strcmp(name, "insertion_follows") == 0) {
        dl->AddLine(p(rMin, rMax, 0.3f, 0.14f), p(rMin, rMax, 0.3f, 0.86f), c, sw);
        dl->AddLine(p(rMin, rMax, 0.42f, 0.5f), p(rMin, rMax, 0.62f, 0.5f), c, sw);
        const ImVec2 pts[3] = {p(rMin, rMax, 0.84f, 0.5f), p(rMin, rMax, 0.64f, 0.34f), p(rMin, rMax, 0.64f, 0.66f)};
        dl->AddConvexPolyFilled(pts, 3, c);
        return;
    }
    dl->AddRect(p(rMin, rMax, 0.25f, 0.25f), p(rMin, rMax, 0.75f, 0.75f), c, 1.0f, 0, sw);
}

bool iconButton(const char* id, const ImVec2& size, const char* iconName, bool active, const char* tooltip, ImU32 activeBg) {
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + size.x, rMin.y + size.y);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = IM_COL32(0x3a, 0x3a, 0x3c, 255);
    if (active) {
        bg = activeBg;
    } else if (held) {
        bg = IM_COL32(0x00, 0x7b, 0xb7, 255);
    } else if (hovered) {
        bg = IM_COL32(0x4e, 0x4e, 0x50, 255);
    }
    dl->AddRectFilled(rMin, rMax, bg, 2.5f);
    dl->AddRect(rMin, rMax, IM_COL32(0x14, 0x14, 0x14, 255), 2.5f, 0, 1.0f);

    const float padX = std::min(4.0f, size.x * 0.15f);
    const float padY = std::min(3.0f, size.y * 0.15f);
    const ImU32 fg = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(214, 214, 214, 255);
    draw(dl, ImVec2(rMin.x + padX, rMin.y + padY), ImVec2(rMax.x - padX, rMax.y - padY), iconName, fg);

    if (tooltip && hovered) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

// after SoundCraft (crates/ui-egui/src/toolbar.rs draw_note_value_icon)
void drawNoteValueIcon(ImDrawList* dl, const ImVec2& rMin, const ImVec2& rMax, TimeGrid::NoteValue value,
                       bool dotted, bool triplet, ImU32 color) {
    if (!dl) return;
    const float stroke = 1.2f;
    const ImVec2 head(rMin.x + 4.0f, rMax.y - 3.5f);
    if (value == TimeGrid::NoteValue::Bar) {
        dl->AddLine(ImVec2(rMin.x + 2.0f, rMin.y + 2.0f), ImVec2(rMin.x + 2.0f, rMax.y - 2.0f), color, stroke);
        dl->AddLine(ImVec2(rMin.x + 5.0f, rMin.y + 2.0f), ImVec2(rMin.x + 5.0f, rMax.y - 2.0f), color, stroke);
        dl->AddLine(ImVec2(rMin.x + 2.0f, rMin.y + 3.0f), ImVec2(rMin.x + 10.0f, rMin.y + 3.0f), color, stroke);
    } else {
        const bool hollow = (value == TimeGrid::NoteValue::Whole || value == TimeGrid::NoteValue::Half);
        if (hollow) {
            dl->AddCircle(head, 2.2f, color, 12, stroke);
        } else {
            dl->AddCircleFilled(head, 2.2f, color, 12);
        }
        if (value != TimeGrid::NoteValue::Whole) {
            const float stemX = head.x + 2.0f;
            dl->AddLine(ImVec2(stemX, head.y), ImVec2(stemX, rMin.y + 2.0f), color, stroke);
            int flags = 0;
            switch (value) {
            case TimeGrid::NoteValue::Eighth: flags = 1; break;
            case TimeGrid::NoteValue::Sixteenth: flags = 2; break;
            case TimeGrid::NoteValue::ThirtySecond: flags = 3; break;
            case TimeGrid::NoteValue::SixtyFourth: flags = 4; break;
            default: flags = 0; break;
            }
            for (int f = 0; f < flags; ++f) {
                const float y = rMin.y + 2.0f + static_cast<float>(f) * 2.0f;
                dl->AddLine(ImVec2(stemX, y), ImVec2(stemX + 4.0f, y + 2.5f), color, stroke);
            }
        }
    }
    if (dotted) {
        dl->AddCircleFilled(ImVec2(rMax.x - 1.5f, head.y), 1.0f, color, 8);
    }
    if (triplet) {
        dl->AddText(rMin, color, "3");
    }
}

// after SoundCraft (crates/ui-egui/src/toolbar.rs compact_grid_label)
std::string compactGridLabel(const Session::Document& session, const TimeGrid::GridValue& value) {
    switch (value.kind) {
    case TimeGrid::GridValue::Kind::Note: {
        const long long ticksPerBeat = (session.time.meter.den > 0) ? (960LL * 4LL / session.time.meter.den) : 960LL;
        const long long ticksPerBar = ticksPerBeat * std::max(1, session.time.meter.num);
        long long ticks = (value.noteVal == TimeGrid::NoteValue::Bar) ? ticksPerBar : TimeGrid::noteTicks(value.noteVal);
        if (value.dotted) ticks = ticks * 3 / 2;
        if (value.triplet) ticks = ticks * 2 / 3;
        const long long perBar = std::max(1LL, ticksPerBar);
        const long long perBeat = std::max(1LL, ticksPerBeat);
        const long long bars = ticks / perBar;
        const long long remaining = ticks % perBar;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%lld|%lld|%03lld", bars, remaining / perBeat, remaining % perBeat);
        return buf;
    }
    case TimeGrid::GridValue::Kind::Seconds: {
        const long long samples = static_cast<long long>(std::llround(value.secondsVal * session.sampleRate));
        return TimeFormat::formatLength(session.sessionEdit.selection.start, samples, TimeFormat::Kind::MinSecs, session);
    }
    case TimeGrid::GridValue::Kind::Frames: {
        const Time::SampleRate sr = Time::SampleRate::fromHz(static_cast<uint32_t>(std::max(1.0, session.sampleRate)));
        const long long samples = Time::samplesAtFrame(session.frameRate, value.framesVal, sr);
        return TimeFormat::formatLength(session.sessionEdit.selection.start, samples, TimeFormat::Kind::Timecode, session) + ".00";
    }
    case TimeGrid::GridValue::Kind::Timecode: {
        switch (value.timecodeVal) {
        case TimeGrid::TimecodeValue::Second: return "00:00:01:00.00";
        case TimeGrid::TimecodeValue::FiveFrames: return "00:00:00:05.00";
        case TimeGrid::TimecodeValue::Frame: return "00:00:00:01.00";
        case TimeGrid::TimecodeValue::HalfFrame: return "00:00:00:00.50";
        case TimeGrid::TimecodeValue::QuarterFrame: return "00:00:00:00.25";
        case TimeGrid::TimecodeValue::SubFrame: return "00:00:00:00.01";
        }
        return "00:00:00:01.00";
    }
    case TimeGrid::GridValue::Kind::FeetFrames: {
        switch (value.feetFramesVal) {
        case TimeGrid::FeetFramesValue::Foot: return "1+00";
        case TimeGrid::FeetFramesValue::Frame: return "0+01";
        case TimeGrid::FeetFramesValue::QuarterFrame: return "0+00.25";
        case TimeGrid::FeetFramesValue::SubFrame: return "0+00.01";
        }
        return "0+01";
    }
    case TimeGrid::GridValue::Kind::Samples:
        return std::to_string(value.samplesVal);
    }
    return "";
}

// after SoundCraft (crates/ui-egui/src/toolbar.rs option_toggles)
std::array<OptionToggleSpec, 6> optionToggles(const Session::Document& doc) {
    auto flag = [&](const char* key, bool def) {
        auto it = doc.sessionEdit.flags.find(key);
        return it != doc.sessionEdit.flags.end() ? it->second : def;
    };
    return {{
        {"options.tab_to_transient", "tab_transient", flag("options.tab_to_transient", false), "Tab to Transient"},
        {"options.link_timeline_edit", "link", flag("options.link_timeline_edit", true), "Link Timeline and Edit Selection"},
        {"options.link_track_edit", "link_track", flag("options.link_track_edit", false), "Link Track and Edit Selection"},
        {"options.insertion_follows_playback", "insertion_follows", flag("options.insertion_follows_playback", false), "Insertion Follows Playback"},
        {"options.mirrored_midi", "note", flag("options.mirrored_midi", false), "Mirrored MIDI Editing"},
        {"options.automation_follows_edit", "wave", doc.edit.automationFollowsEdit, "Automation Follows Edit"},
    }};
}

} // namespace Icons
