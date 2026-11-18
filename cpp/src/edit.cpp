// Polyhymnia - Edit window: toolbar, 5 rulers, track headers, clips, MIDI notes, automation, Fades dialog
// (after SoundCraft crates/ui-egui/src/toolbar.rs, edit_window/*.rs, dialogs.rs).
#include "edit.h"
#include "app.h"
#include "clip_ops.h"
#include "clips_panel.h"
#include "edit_drag.h"
#include "editor.h"
#include "engine.h"
#include "icons.h"
#include "lang.h"
#include "mix.h"
#include "playback.h"
#include "record_bounce.h"
#include "spot_dialog.h"
#include "tempo.h"
#include "time_format.h"
#include "time_grid.h"
#include "time_range.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace Edit {

static float gSamplesPerPixel = 200.0f;
static float gWaveformScale = 1.0f;
static float gZoomPresets[5] = {64.0f, 128.0f, 200.0f, 512.0f, 1024.0f};
static bool gZoomToggleActive = false;
static bool gKeyboardFocus = true;
static TimeFormat::Kind gMainCounter = TimeFormat::Kind::BarsBeats;
static TimeFormat::Kind gSubCounter = TimeFormat::Kind::MinSecs;
static float scrollX = 0.0f;
static float scrollY = 0.0f;

static constexpr float kHeaderW = 200.0f;
static constexpr float kRulerBarsH = 22.0f;
static constexpr float kRulerTimeH = 18.0f;
static constexpr float kRulerTempoH = 16.0f;
static constexpr float kRulerMeterH = 16.0f;
static constexpr float kRulerMarkersH = 18.0f;
static constexpr float kRulersH = kRulerBarsH + kRulerTimeH + kRulerTempoH + kRulerMeterH + kRulerMarkersH; // 90.0f

// Atelier & SoundCraft token palette (after crates/ui-egui/src/theme.rs)
static const ImU32 C_BG0 = IM_COL32(0x16, 0x16, 0x16, 255);
static const ImU32 C_BG1 = IM_COL32(0x1e, 0x1e, 0x1e, 255);
static const ImU32 C_BG2 = IM_COL32(0x25, 0x25, 0x25, 255);
static const ImU32 C_BG3 = IM_COL32(0x2e, 0x2e, 0x2e, 255);
static const ImU32 C_BG4 = IM_COL32(0x3a, 0x3a, 0x3a, 255);
static const ImU32 C_LCD = IM_COL32(0x0f, 0x13, 0x11, 255);
static const ImU32 C_LCD_TEXT = IM_COL32(0x7c, 0xf2, 0x9c, 255);
static const ImU32 C_LCD_DIM = IM_COL32(0x3b, 0x6e, 0x4b, 255);
static const ImU32 C_BORDER = IM_COL32(0x0c, 0x0c, 0x0c, 255);
static const ImU32 C_BEVEL = IM_COL32(0x3c, 0x3c, 0x3c, 255);
static const ImU32 C_TEXT = IM_COL32(0xe2, 0xe2, 0xe2, 255);
static const ImU32 C_DIM = IM_COL32(0x96, 0x96, 0x96, 255);
static const ImU32 C_MUTED = IM_COL32(0x64, 0x64, 0x64, 255);
static const ImU32 C_BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255);
static const ImU32 C_PLAY = IM_COL32(0x28, 0xc8, 0x54, 255);
static const ImU32 C_REC = IM_COL32(0xe6, 0x32, 0x32, 255);
static const ImU32 C_SOLO = IM_COL32(0xe8, 0xc2, 0x28, 255);
static const ImU32 C_MUTE = IM_COL32(0xe0, 0x7a, 0x22, 255);
static const ImU32 C_SHUFFLE = IM_COL32(0xc9, 0x42, 0x42, 255);
static const ImU32 C_SLIP = IM_COL32(0x3f, 0xa8, 0x56, 255);
static const ImU32 C_SPOT = IM_COL32(0xd6, 0x86, 0x2c, 255);
static const ImU32 C_GRID = IM_COL32(0x3a, 0x84, 0xd8, 255);

// after SoundCraft edit_window/input.rs gestures + drag.rs drive_drag / finish_drag
enum class GestureKind { None, Select, MoveClips, TrimStart, TrimEnd, ResizeTrack, DragVol, DragPan };

struct Gesture {
    GestureKind kind = GestureKind::None;
    long long anchor = 0;
    std::string selTrack;
    std::vector<std::string> clipIds;
    long long grabAt = 0;
    long long delta = 0;
    long long trimTo = 0;
    std::string trimClip;
    std::string fromTrack;
    std::string toTrack;
    int trackIdx = -1;
    float startMouseY = 0.0f;
    float startMouseX = 0.0f;
    float startVal = 0.0f;
};

static Gesture gGesture;
static bool gHadDrag = false;
static ImVec2 gTimelineMin = ImVec2(0, 0);
static ImVec2 gTimelineMax = ImVec2(0, 0);
static float gRulerTopY = 0.0f;

// Modal dialog states
struct FadesDialogState {
    bool open = false;
    Session::FadeShape shape = Session::FadeShape::EqualPower;
    float curvature = 0.0f;
    float defaultFadeMs = 50.0f;
};
static FadesDialogState gFadesDlg;

struct RenameModalState {
    bool open = false;
    int kind = 0; // 0 = Track, 1 = Marker, 2 = TempoChange
    std::string targetId;
    uint64_t targetNumId = 0;
    long long sampleAt = 0;
    char nameBuf[256] = "";
    float bpm = 120.0f;
};
static RenameModalState gRenameModal;

void openFadesDialog() {
    gFadesDlg.open = true;
}

static bool getSessionFlag(const Session::Document& doc, const char* key, bool defVal = false) {
    auto it = doc.sessionEdit.flags.find(key);
    return it != doc.sessionEdit.flags.end() ? it->second : defVal;
}

static void toggleSessionFlag(Session::Document& doc, const char* key, bool defVal = false) {
    doc.sessionEdit.flags[key] = !getSessionFlag(doc, key, defVal);
}

static float trackHeightPx(const Session::Track& t) {
    return std::clamp(t.heightPoints > 0.0f ? t.heightPoints : 88.0f, 26.0f, 360.0f);
}

static float trackTopOffset(const Session::Document& doc, int trackIdx) {
    float y = 0.0f;
    for (int i = 0; i < trackIdx && i < (int)doc.tracks.size(); ++i) {
        if (!doc.tracks[(size_t)i].hidden) {
            y += trackHeightPx(doc.tracks[(size_t)i]);
        }
    }
    return y;
}

static float totalTracksHeight(const Session::Document& doc) {
    float h = 0.0f;
    for (const auto& t : doc.tracks) {
        if (!t.hidden) h += trackHeightPx(t);
    }
    return h;
}

static int trackRowAt(const Session::Document& doc, float contentY, float rulerTop) {
    const float relY = contentY - rulerTop;
    if (relY < 0.0f) return -1;
    float acc = 0.0f;
    for (int i = 0; i < (int)doc.tracks.size(); ++i) {
        if (doc.tracks[(size_t)i].hidden) continue;
        const float th = trackHeightPx(doc.tracks[(size_t)i]);
        if (relY >= acc && relY < acc + th) return i;
        acc += th;
    }
    return -1;
}

static std::string trackAtY(const Session::Document& doc, float contentY, float rulerTop) {
    const int row = trackRowAt(doc, contentY, rulerTop);
    return row >= 0 ? doc.tracks[(size_t)row].id : std::string{};
}

static const Session::Clip* clipAt(const Session::Track& track, long long at) {
    for (const auto& c : track.clips)
        if (at >= c.start && at < ClipOps::end(c)) return &c;
    return nullptr;
}

static std::optional<bool> nearClipEdge(float x0, float x1, float mx) {
    if (std::abs(mx - x0) < 6.0f) return true;
    if (std::abs(mx - x1) < 6.0f) return false;
    return std::nullopt;
}

// after SoundCraft edit_window/input.rs effective_tool
static Editor::Tool effectiveTool(Editor::Tool tool, const Session::Clip* hit, float laneMidY, float my, float mx, float x0, float x1) {
    if (tool != Editor::Tool::Smart) return tool;
    if (!hit) return Editor::Tool::Selector;
    if (nearClipEdge(x0, x1, mx)) return Editor::Tool::Trim;
    return my < laneMidY ? Editor::Tool::Selector : Editor::Tool::Grabber;
}

static void finishGesture(Session::Document& doc) {
    if (gGesture.kind == GestureKind::None) return;
    const Gesture g = gGesture;
    gGesture = {};
    gHadDrag = false;
    if (g.kind == GestureKind::MoveClips) {
        std::optional<std::string> toT;
        if (g.toTrack != g.fromTrack) toT = g.toTrack;
        const auto ordered = EditDrag::grabbedFirst(doc, g.clipIds, g.fromTrack);
        auto spot = Spot::clips(doc, ordered, toT, g.delta);
        if (spot) {
            spot->fromTrack = g.fromTrack;
            if (Editor::mode() == Editor::Mode::Spot) {
                Spot::open(std::move(*spot));
            } else {
                const Spot::State st = *spot;
                App::mutateDocument("Move clips", [&](Session::Document& d) { Spot::apply(st, d); });
            }
        }
    } else if (g.kind == GestureKind::TrimStart) {
        auto spot = Spot::trim(doc, g.trimClip, true, g.trimTo);
        if (spot) {
            if (Editor::mode() == Editor::Mode::Spot) {
                Spot::open(std::move(*spot));
            } else {
                const Spot::State st = *spot;
                App::mutateDocument("Trim start", [&](Session::Document& d) { Spot::apply(st, d); });
            }
        }
    } else if (g.kind == GestureKind::TrimEnd) {
        auto spot = Spot::trim(doc, g.trimClip, false, g.trimTo);
        if (spot) {
            if (Editor::mode() == Editor::Mode::Spot) {
                Spot::open(std::move(*spot));
            } else {
                const Spot::State st = *spot;
                App::mutateDocument("Trim end", [&](Session::Document& d) { Spot::apply(st, d); });
            }
        }
    }
}

static void driveGesture(Session::Document& doc, float p0x, float headerW, float rulerTop) {
    if (gGesture.kind == GestureKind::None) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.MouseDown[0]) {
        finishGesture(doc);
        return;
    }
    const long long visStart = (long long)scrollX;
    const float mx = io.MousePos.x;
    const float my = io.MousePos.y;
    const long long at = std::max(0LL, visStart + (long long)((mx - (p0x + headerW)) * gSamplesPerPixel));
    const bool flip = io.KeyCtrl;
    if (gGesture.kind == GestureKind::Select) {
        const long long end = EditDrag::dragSnap(doc, at, flip);
        const long long s0 = std::min(gGesture.anchor, end);
        const long long s1 = std::max(gGesture.anchor, end);
        Editor::selectRange(s0, s1, gGesture.selTrack);
        doc.sessionEdit.selection = Time::Range{s0, s1};
    } else if (gGesture.kind == GestureKind::MoveClips) {
        const std::string toTr = trackAtY(doc, my, rulerTop);
        if (!toTr.empty()) gGesture.toTrack = toTr;
        const auto plan = EditDrag::dragPlan(doc, doc.touch, gGesture.clipIds, gGesture.fromTrack, gGesture.toTrack, gGesture.grabAt);
        const long long raw = Time::satSub(at, gGesture.grabAt);
        gGesture.delta = EditDrag::moveDelta(*plan, doc, gGesture.fromTrack, raw, flip);
        gHadDrag = true;
    } else if (gGesture.kind == GestureKind::TrimStart) {
        gGesture.trimTo = EditDrag::trimDragSnap(doc, gGesture.trimClip, at, true, flip);
    } else if (gGesture.kind == GestureKind::TrimEnd) {
        gGesture.trimTo = EditDrag::trimDragSnap(doc, gGesture.trimClip, at, false, flip);
    } else if (gGesture.kind == GestureKind::ResizeTrack) {
        if (gGesture.trackIdx >= 0 && gGesture.trackIdx < (int)doc.tracks.size()) {
            doc.tracks[(size_t)gGesture.trackIdx].heightPoints = std::clamp(gGesture.startVal + (my - gGesture.startMouseY), 26.0f, 360.0f);
        }
    } else if (gGesture.kind == GestureKind::DragVol) {
        if (gGesture.trackIdx >= 0 && gGesture.trackIdx < (int)doc.tracks.size()) {
            doc.tracks[(size_t)gGesture.trackIdx].gainDb = std::clamp(gGesture.startVal - (my - gGesture.startMouseY) * 0.25f, -60.0f, 12.0f);
            ++doc.touch;
        }
    } else if (gGesture.kind == GestureKind::DragPan) {
        if (gGesture.trackIdx >= 0 && gGesture.trackIdx < (int)doc.tracks.size()) {
            doc.tracks[(size_t)gGesture.trackIdx].pan = std::clamp(gGesture.startVal + (mx - gGesture.startMouseX) * 0.01f, -1.0f, 1.0f);
            ++doc.touch;
        }
    }
}

// ============================================================================
// Top Toolbar (after SoundCraft crates/ui-egui/src/toolbar.rs)
// ============================================================================

static void drawToolbarFrame(ImDrawList* dl, ImVec2 min, ImVec2 max) {
    dl->AddRectFilled(min, max, C_BG2, 3.0f);
    dl->AddRect(min, max, C_BORDER, 3.0f);
    dl->AddLine(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, min.y + 1.0f), C_BEVEL);
}

static bool drawModeCell(const char* id, const char* label, bool active, ImU32 activeCol, ImVec2 size, bool lockIcon = false) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 br(pos.x + size.x, pos.y + size.y);
    const ImU32 fill = active ? activeCol : (hovered ? C_BG4 : C_BG3);
    dl->AddRectFilled(pos, br, fill, 2.0f);
    dl->AddRect(pos, br, C_BORDER, 2.0f);
    if (active) {
        dl->AddLine(ImVec2(pos.x + 1, pos.y + 1), ImVec2(br.x - 1, pos.y + 1), IM_COL32(255, 255, 255, 80));
    }
    const ImU32 fg = active ? IM_COL32(255, 255, 255, 255) : C_TEXT;
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float tx = lockIcon ? (pos.x + (size.x - ts.x) * 0.5f - 4.0f) : (pos.x + (size.x - ts.x) * 0.5f);
    dl->AddText(ImVec2(tx, pos.y + (size.y - ts.y) * 0.5f), fg, label);
    if (lockIcon) {
        Icons::draw(dl, ImVec2(br.x - 12.0f, pos.y + size.y * 0.5f - 4.5f), ImVec2(br.x - 3.0f, pos.y + size.y * 0.5f + 4.5f), "lock",
                    active ? IM_COL32(255, 255, 255, 220) : C_MUTED);
    }
    return clicked;
}

static void drawGridValueMenu(const char* popupId, TimeGrid::GridValue& gv, Session::Document& doc) {
    if (ImGui::BeginPopup(popupId)) {
        const TimeGrid::GridScale curScale = gv.scale();
        auto scaleItem = [&](const char* lbl, TimeGrid::GridScale sc) {
            if (ImGui::MenuItem(lbl, nullptr, curScale == sc)) {
                gv = TimeGrid::GridValue::forScale(sc);
                ++doc.touch;
            }
        };
        scaleItem("Bars|Beats", TimeGrid::GridScale::BarsBeats);
        scaleItem("Min:Secs", TimeGrid::GridScale::MinSecs);
        scaleItem("Timecode", TimeGrid::GridScale::Timecode);
        scaleItem("Feet+Frames", TimeGrid::GridScale::FeetFrames);
        scaleItem("Samples", TimeGrid::GridScale::Samples);
        if (&gv == &doc.edit.grid) {
            if (ImGui::MenuItem("Clips/Markers", nullptr, doc.edit.gridClipsMarkers)) {
                doc.edit.gridClipsMarkers = !doc.edit.gridClipsMarkers;
                ++doc.touch;
            }
        }
        ImGui::Separator();
        if (gv.kind == TimeGrid::GridValue::Kind::Note) {
            for (const auto nv : TimeGrid::NOTE_ALL) {
                if (ImGui::MenuItem(TimeGrid::noteLabel(nv), nullptr, gv.noteVal == nv)) {
                    gv.noteVal = nv;
                    ++doc.touch;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Triplet", nullptr, gv.triplet)) {
                gv.triplet = !gv.triplet;
                if (gv.triplet) gv.dotted = false;
                ++doc.touch;
            }
            if (ImGui::MenuItem("Dotted", nullptr, gv.dotted)) {
                gv.dotted = !gv.dotted;
                if (gv.dotted) gv.triplet = false;
                ++doc.touch;
            }
        } else if (gv.kind == TimeGrid::GridValue::Kind::Seconds) {
            static const struct { const char* name; double sec; } kMs[] = {
                {"1 sec", 1.0}, {"500 ms", 0.5}, {"100 ms", 0.1}, {"10 ms", 0.01}, {"1 ms", 0.001},
            };
            for (const auto& m : kMs) {
                if (ImGui::MenuItem(m.name, nullptr, std::abs(gv.secondsVal - m.sec) < 1e-6)) {
                    gv.secondsVal = m.sec;
                    ++doc.touch;
                }
            }
        } else if (gv.kind == TimeGrid::GridValue::Kind::Timecode) {
            for (const auto tv : TimeGrid::TIMECODE_ALL) {
                if (ImGui::MenuItem(TimeGrid::timecodeValueLabel(tv), nullptr, gv.timecodeVal == tv)) {
                    gv.timecodeVal = tv;
                    ++doc.touch;
                }
            }
        } else if (gv.kind == TimeGrid::GridValue::Kind::FeetFrames) {
            for (const auto fv : TimeGrid::FEET_FRAMES_ALL) {
                if (ImGui::MenuItem(TimeGrid::feetFramesValueLabel(fv), nullptr, gv.feetFramesVal == fv)) {
                    gv.feetFramesVal = fv;
                    ++doc.touch;
                }
            }
        } else if (gv.kind == TimeGrid::GridValue::Kind::Samples) {
            for (int64_t s : {1LL, 10LL, 100LL, 1000LL, 10000LL}) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld samples", (long long)s);
                if (ImGui::MenuItem(buf, nullptr, gv.samplesVal == s)) {
                    gv.samplesVal = s;
                    ++doc.touch;
                }
            }
        }
        ImGui::EndPopup();
    }
}

void drawToolbar(Session::Document& doc) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 4.0f));
    ImGui::BeginChild("sc_toolbar", ImVec2(0, 76.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 winMin = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    dl->AddRectFilled(winMin, ImVec2(winMin.x + winSize.x, winMin.y + winSize.y), C_BG1);
    dl->AddLine(ImVec2(winMin.x, winMin.y + winSize.y - 1.0f), ImVec2(winMin.x + winSize.x, winMin.y + winSize.y - 1.0f), C_BORDER);

    const float groupY = winMin.y + 5.0f;
    const float groupH = 66.0f;
    float curX = winMin.x + 6.0f;

    // 1. Edit Modes 2x2 (SHUFFLE, SPOT / SLIP, GRID) - after toolbar.rs edit_modes_group
    {
        const float gw = 136.0f;
        drawToolbarFrame(dl, ImVec2(curX, groupY), ImVec2(curX + gw, groupY + groupH));
        const ImVec2 btnSz(61.0f, 26.0f);
        ImGui::SetCursorScreenPos(ImVec2(curX + 5.0f, groupY + 5.0f));
        if (drawModeCell("mode_shuffle", "SHUFFLE", Editor::mode() == Editor::Mode::Shuffle, C_SHUFFLE, btnSz, true)) {
            Editor::setMode(Editor::Mode::Shuffle);
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (drawModeCell("mode_spot", "SPOT", Editor::mode() == Editor::Mode::Spot, C_SPOT, btnSz)) {
            Editor::setMode(Editor::Mode::Spot);
        }
        ImGui::SetCursorScreenPos(ImVec2(curX + 5.0f, groupY + 35.0f));
        if (drawModeCell("mode_slip", "SLIP", Editor::mode() == Editor::Mode::Slip, C_SLIP, btnSz)) {
            Editor::setMode(Editor::Mode::Slip);
        }
        ImGui::SameLine(0.0f, 4.0f);
        const bool isGrid = Editor::mode() == Editor::Mode::Grid || Editor::mode() == Editor::Mode::GridRelative;
        const char* gridLbl = Editor::mode() == Editor::Mode::GridRelative ? "REL GRID" : "GRID";
        if (drawModeCell("mode_grid", gridLbl, isGrid, C_GRID, btnSz)) {
            if (Editor::mode() == Editor::Mode::Grid) Editor::setMode(Editor::Mode::GridRelative);
            else Editor::setMode(Editor::Mode::Grid);
        }
        curX += gw + 5.0f;
    }

    // 2. Zoom Controls & Presets 1-5 - after toolbar.rs zoom_group
    {
        const float gw = 114.0f;
        drawToolbarFrame(dl, ImVec2(curX, groupY), ImVec2(curX + gw, groupY + groupH));
        ImGui::SetCursorScreenPos(ImVec2(curX + 5.0f, groupY + 6.0f));
        if (Icons::iconButton("zm_out", ImVec2(20, 22), "minus", false, "Horizontal Zoom Out (R)", C_BLUE)) {
            gSamplesPerPixel = std::clamp(gSamplesPerPixel * 1.25f, 1.0f, 100000.0f);
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Icons::iconButton("zm_in", ImVec2(20, 22), "plus", false, "Horizontal Zoom In (T)", C_BLUE)) {
            gSamplesPerPixel = std::clamp(gSamplesPerPixel * 0.8f, 1.0f, 100000.0f);
        }
        ImGui::SameLine(0.0f, 4.0f);
        const ImVec2 wfPos = ImGui::GetCursorScreenPos();
        Icons::draw(dl, ImVec2(wfPos.x + 1, wfPos.y + 4), ImVec2(wfPos.x + 13, wfPos.y + 18), "wave", C_DIM);
        ImGui::Dummy(ImVec2(14, 22));
        ImGui::SameLine(0.0f, 2.0f);
        if (Icons::iconButton("wf_down", ImVec2(20, 22), "minus", false, "Waveform Height Down", C_BLUE)) {
            gWaveformScale = std::clamp(gWaveformScale * 0.8f, 0.25f, 16.0f);
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Icons::iconButton("wf_up", ImVec2(20, 22), "plus", false, "Waveform Height Up", C_BLUE)) {
            gWaveformScale = std::clamp(gWaveformScale * 1.25f, 0.25f, 16.0f);
        }

        // Bottom row: Zoom Presets 1..5
        for (int i = 0; i < 5; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(curX + 5.0f + i * 21.0f, groupY + 35.0f));
            char id[16], lbl[8];
            std::snprintf(id, sizeof(id), "zp_%d", i + 1);
            std::snprintf(lbl, sizeof(lbl), "%d", i + 1);
            const float presetSpp = gZoomPresets[(size_t)i];
            const bool active = std::abs(gSamplesPerPixel - presetSpp) < 0.5f;
            if (drawModeCell(id, lbl, active, C_BLUE, ImVec2(19.0f, 24.0f))) {
                gSamplesPerPixel = presetSpp;
            }
        }
        curX += gw + 5.0f;
    }

    // 3. Tools & Option Toggles (ZoomToggle, Zoom, Trim, Selector, Grabber, Scrubber, Pencil + Smart bracket) - after toolbar.rs tools_group
    {
        const float gw = 198.0f;
        drawToolbarFrame(dl, ImVec2(curX, groupY), ImVec2(curX + gw, groupY + groupH));

        // Smart Tool bracket bar across Trim, Selector, Grabber
        const float smartX = curX + 5.0f + 2 * 26.0f;
        const float smartW = 3 * 24.0f + 2 * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(smartX, groupY + 3.0f));
        const bool isSmart = Editor::tool() == Editor::Tool::Smart;
        if (ImGui::InvisibleButton("smart_bracket", ImVec2(smartW, 7.0f))) {
            Editor::setTool(Editor::Tool::Smart);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smart Tool (F6+F7)");
        dl->AddRectFilled(ImVec2(smartX, groupY + 3.0f), ImVec2(smartX + smartW, groupY + 9.0f),
                          isSmart ? C_BLUE : (ImGui::IsItemHovered() ? C_BG4 : C_BG3), 1.5f);

        // Tool row
        ImGui::SetCursorScreenPos(ImVec2(curX + 5.0f, groupY + 11.0f));
        if (Icons::iconButton("zt_btn", ImVec2(24, 24), "zoom_toggle", gZoomToggleActive, "Zoom Toggle (E)", C_BLUE)) {
            gZoomToggleActive = !gZoomToggleActive;
        }
        ImGui::SameLine(0.0f, 2.0f);
        auto toolBtn = [&](const char* id, const char* iconName, Editor::Tool t, bool smartMember, const char* tip) {
            const bool active = Editor::tool() == t || (isSmart && smartMember);
            if (Icons::iconButton(id, ImVec2(24, 24), iconName, active, tip, C_BLUE)) {
                Editor::setTool(t);
            }
            ImGui::SameLine(0.0f, 2.0f);
        };
        toolBtn("tl_zoom", "zoom", Editor::Tool::Zoom, false, "Zoomer Tool (F5)");
        toolBtn("tl_trim", "trim", Editor::Tool::Trim, true, "Trim Tool (F6)");
        toolBtn("tl_sel", "selector", Editor::Tool::Selector, true, "Selector Tool (F7)");
        toolBtn("tl_grab", "grabber", Editor::Tool::Grabber, true, "Grabber Tool (F8)");
        toolBtn("tl_scrub", "scrubber", Editor::Tool::Scrubber, false, "Scrubber Tool (F9)");
        if (Icons::iconButton("tl_pencil", ImVec2(24, 24), "pencil", Editor::tool() == Editor::Tool::Pencil, "Pencil Tool (F10)", C_BLUE)) {
            Editor::setTool(Editor::Tool::Pencil);
        }

        // Bottom row: 6 option icon toggles from Icons::optionToggles(doc) + a-z Keyboard Focus
        ImGui::SetCursorScreenPos(ImVec2(curX + 5.0f, groupY + 38.0f));
        const auto toggles = Icons::optionToggles(doc);
        for (size_t i = 0; i < toggles.size(); ++i) {
            char tid[32];
            std::snprintf(tid, sizeof(tid), "opt_tgl_%zu", i);
            if (Icons::iconButton(tid, ImVec2(22, 22), toggles[i].icon, toggles[i].on, toggles[i].tooltip, C_BLUE)) {
                if (std::strcmp(toggles[i].commandId, "options.automation_follows_edit") == 0) {
                    doc.edit.automationFollowsEdit = !doc.edit.automationFollowsEdit;
                } else {
                    const bool def = std::strcmp(toggles[i].commandId, "options.link_timeline_edit") == 0;
                    toggleSessionFlag(doc, toggles[i].commandId, def);
                }
            }
            ImGui::SameLine(0.0f, 2.0f);
        }
        if (drawModeCell("opt_az", "a-z", gKeyboardFocus, C_SOLO, ImVec2(32, 22))) {
            gKeyboardFocus = !gKeyboardFocus;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Commands Keyboard Focus");
        curX += gw + 5.0f;
    }

    // 4. Main & Sub Counter LCD + Start/End/Length - after toolbar.rs counter_display
    {
        const float gw = 294.0f;
        const ImVec2 lcdMin(curX, groupY);
        const ImVec2 lcdMax(curX + gw, groupY + groupH);
        dl->AddRectFilled(lcdMin, lcdMax, C_LCD, 3.0f);
        dl->AddRect(lcdMin, lcdMax, C_BORDER, 3.0f);

        const long long head = Playback::playhead();
        const std::string mainStr = TimeFormat::formatPosition(head, gMainCounter, doc);
        const std::string subStr = TimeFormat::formatPosition(head, gSubCounter, doc);

        // Main counter label + dropdown
        dl->AddText(ImVec2(curX + 8.0f, groupY + 4.0f), C_LCD_DIM, TimeFormat::label(gMainCounter));
        Icons::draw(dl, ImVec2(curX + 142.0f, groupY + 6.0f), ImVec2(curX + 150.0f, groupY + 14.0f), "triangle_down", C_LCD_DIM);
        ImGui::SetCursorScreenPos(ImVec2(curX + 4.0f, groupY + 2.0f));
        if (ImGui::InvisibleButton("main_cnt_fmt", ImVec2(150.0f, 34.0f))) {
            ImGui::OpenPopup("main_counter_popup");
        }
        if (ImGui::BeginPopup("main_counter_popup")) {
            for (const auto f : TimeFormat::ALL) {
                if (ImGui::MenuItem(TimeFormat::label(f), nullptr, gMainCounter == f)) {
                    gMainCounter = f;
                }
            }
            ImGui::EndPopup();
        }

        // Large main counter text
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.38f, ImVec2(curX + 8.0f, groupY + 16.0f), C_LCD_TEXT, mainStr.c_str());

        // Divider + Sub counter
        dl->AddLine(ImVec2(curX + 6.0f, groupY + 41.0f), ImVec2(curX + 152.0f, groupY + 41.0f), IM_COL32(0x1f, 0x2b, 0x22, 255));
        dl->AddText(ImVec2(curX + 8.0f, groupY + 45.0f), C_LCD_DIM, TimeFormat::label(gSubCounter));
        dl->AddText(ImVec2(curX + 68.0f, groupY + 45.0f), C_LCD_TEXT, subStr.c_str());
        Icons::draw(dl, ImVec2(curX + 142.0f, groupY + 47.0f), ImVec2(curX + 150.0f, groupY + 55.0f), "triangle_down", C_LCD_DIM);
        ImGui::SetCursorScreenPos(ImVec2(curX + 4.0f, groupY + 42.0f));
        if (ImGui::InvisibleButton("sub_cnt_fmt", ImVec2(150.0f, 22.0f))) {
            ImGui::OpenPopup("sub_counter_popup");
        }
        if (ImGui::BeginPopup("sub_counter_popup")) {
            for (const auto f : TimeFormat::ALL) {
                if (ImGui::MenuItem(TimeFormat::label(f), nullptr, gSubCounter == f)) {
                    gSubCounter = f;
                }
            }
            ImGui::EndPopup();
        }

        // Vertical divider between Main/Sub and Start/End/Length
        dl->AddLine(ImVec2(curX + 158.0f, groupY + 4.0f), ImVec2(curX + 158.0f, groupY + groupH - 4.0f), IM_COL32(0x1f, 0x2b, 0x22, 255));

        const Time::Range sel = (Editor::selection().end > Editor::selection().start)
            ? Time::Range{Editor::selection().start, Editor::selection().end}
            : doc.sessionEdit.selection;
        const std::string sStart = TimeFormat::formatPosition(sel.start, gMainCounter, doc);
        const std::string sEnd = TimeFormat::formatPosition(sel.end, gMainCounter, doc);
        const std::string sLen = TimeFormat::formatLength(sel.start, sel.len(), gMainCounter, doc);

        dl->AddText(ImVec2(curX + 164.0f, groupY + 6.0f), C_LCD_DIM, "Start");
        dl->AddText(ImVec2(curX + 206.0f, groupY + 6.0f), C_LCD_TEXT, sStart.c_str());
        dl->AddText(ImVec2(curX + 164.0f, groupY + 25.0f), C_LCD_DIM, "End");
        dl->AddText(ImVec2(curX + 206.0f, groupY + 25.0f), C_LCD_TEXT, sEnd.c_str());
        dl->AddText(ImVec2(curX + 164.0f, groupY + 44.0f), C_LCD_DIM, "Length");
        dl->AddText(ImVec2(curX + 206.0f, groupY + 44.0f), C_LCD_TEXT, sLen.c_str());

        curX += gw + 5.0f;
    }

    // 5. Grid & Nudge compact value readouts - after toolbar.rs grid_nudge_group
    {
        const float gw = 112.0f;
        drawToolbarFrame(dl, ImVec2(curX, groupY), ImVec2(curX + gw, groupY + groupH));

        auto drawCompactRow = [&](const char* btnId, const char* popupId, const char* title, TimeGrid::GridValue& gv, float ry) {
            dl->AddText(ImVec2(curX + 6.0f, ry + 5.0f), C_DIM, title);
            const ImVec2 boxMin(curX + 44.0f, ry + 1.0f);
            const ImVec2 boxMax(curX + gw - 5.0f, ry + 26.0f);
            dl->AddRectFilled(boxMin, boxMax, C_LCD, 2.0f);
            dl->AddRect(boxMin, boxMax, C_BORDER, 2.0f);
            float textX = boxMin.x + 5.0f;
            if (gv.kind == TimeGrid::GridValue::Kind::Note) {
                Icons::drawNoteValueIcon(dl, ImVec2(textX, boxMin.y + 3.0f), ImVec2(textX + 10.0f, boxMax.y - 3.0f),
                                         gv.noteVal, gv.dotted, gv.triplet, C_LCD_TEXT);
                textX += 12.0f;
            }
            const std::string lbl = Icons::compactGridLabel(doc, gv);
            dl->PushClipRect(ImVec2(textX, boxMin.y), ImVec2(boxMax.x - 12.0f, boxMax.y), true);
            dl->AddText(ImVec2(textX, boxMin.y + 5.0f), C_LCD_TEXT, lbl.c_str());
            dl->PopClipRect();
            Icons::draw(dl, ImVec2(boxMax.x - 10.0f, boxMin.y + 9.0f), ImVec2(boxMax.x - 3.0f, boxMin.y + 16.0f), "triangle_down", C_LCD_DIM);

            ImGui::SetCursorScreenPos(boxMin);
            if (ImGui::InvisibleButton(btnId, ImVec2(boxMax.x - boxMin.x, boxMax.y - boxMin.y))) {
                ImGui::OpenPopup(popupId);
            }
            drawGridValueMenu(popupId, gv, doc);
        };

        drawCompactRow("grid_val_btn", "grid_val_popup", "Grid", doc.edit.grid, groupY + 5.0f);
        drawCompactRow("nudge_val_btn", "nudge_val_popup", "Nudge", doc.edit.nudge, groupY + 35.0f);
        curX += gw + 5.0f;
    }

    // 6. Transport buttons (Top: loop, stop, play, record; Bottom: rtz, rewind, ffwd, end) - after toolbar.rs transport_group
    {
        const float gw = 138.0f;
        drawToolbarFrame(dl, ImVec2(curX, groupY), ImVec2(curX + gw, groupY + groupH));
        const ImVec2 tSz(30.0f, 26.0f);
        ImGui::SetCursorScreenPos(ImVec2(curX + 6.0f, groupY + 5.0f));
        if (Icons::iconButton("tr_loop", tSz, "loop", Playback::loopEnabled(), "Loop Playback (Ctrl+Shift+L)", C_BLUE)) {
            Playback::toggleLoop();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Icons::iconButton("tr_stop", tSz, "stop", false, "Stop (Space)", C_BLUE)) {
            if (RecordBounce::isRecording()) {
                std::string note;
                RecordBounce::stopRecording(doc, note);
            }
            Playback::stop();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Icons::iconButton("tr_play", tSz, "play", Playback::isPlaying(), "Play (Space)", C_PLAY)) {
            if (RecordBounce::isRecording()) {
                std::string note;
                RecordBounce::stopRecording(doc, note);
            } else if (Playback::isPlaying()) {
                Playback::stop();
            } else {
                Playback::play();
            }
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Icons::iconButton("tr_rec", tSz, "record", RecordBounce::isRecording(), "Record (F12 / Ctrl+Space)", C_REC)) {
            std::string note;
            RecordBounce::toggleRecording(doc, note);
        }

        // Bottom row: rtz, rewind, ffwd, end
        ImGui::SetCursorScreenPos(ImVec2(curX + 6.0f, groupY + 35.0f));
        if (Icons::iconButton("tr_rtz", tSz, "rtz", false, "Return to Zero (Home)", C_BLUE)) {
            Playback::setPlayhead(0);
            scrollX = 0.0f;
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Icons::iconButton("tr_rew", tSz, "rewind", false, "Rewind", C_BLUE)) {
            const long long step = std::max(1LL, Editor::gridStepSamples(doc));
            Playback::setPlayhead(std::max(0LL, Playback::playhead() - step));
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Icons::iconButton("tr_ffwd", tSz, "ffwd", false, "Fast Forward", C_BLUE)) {
            const long long step = std::max(1LL, Editor::gridStepSamples(doc));
            Playback::setPlayhead(Playback::playhead() + step);
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Icons::iconButton("tr_end", tSz, "end", false, "Go to End (End)", C_BLUE)) {
            long long maxEnd = 0;
            for (const auto& t : doc.tracks) {
                for (const auto& c : t.clips) maxEnd = std::max(maxEnd, ClipOps::end(c));
            }
            Playback::setPlayhead(maxEnd);
        }
        curX += gw + 5.0f;
    }

    // 7. Count Off, Meter, Tempo & Click - after toolbar.rs tempo_meter_group
    {
        const float gw = 148.0f;
        drawToolbarFrame(dl, ImVec2(curX, groupY), ImVec2(curX + gw, groupY + groupH));
        ImGui::SetCursorScreenPos(ImVec2(curX + 6.0f, groupY + 6.0f));
        if (drawModeCell("cnt_off_btn", "Count Off", doc.countoff, C_BLUE, ImVec2(66.0f, 22.0f))) {
            doc.countoff = !doc.countoff;
        }
        char barsLbl[16];
        std::snprintf(barsLbl, sizeof(barsLbl), "%d bars", doc.countoffBars);
        dl->AddText(ImVec2(curX + 78.0f, groupY + 10.0f), C_DIM, barsLbl);
        dl->AddText(ImVec2(curX + 118.0f, groupY + 10.0f), C_TEXT,
                    (std::to_string(doc.time.meter.num) + "/" + std::to_string(doc.time.meter.den)).c_str());

        // Bottom row: Tempo BPM + Metronome Click icon
        dl->AddText(ImVec2(curX + 6.0f, groupY + 40.0f), C_DIM, "Tempo");
        ImGui::SetCursorScreenPos(ImVec2(curX + 46.0f, groupY + 36.0f));
        ImGui::SetNextItemWidth(64.0f);
        float bpm = (float)doc.time.tempoBpm;
        if (ImGui::DragFloat("##toolbar_bpm", &bpm, 0.2f, 20.0f, 300.0f, "%.1f")) {
            doc.time.tempoBpm = std::clamp((double)bpm, 20.0, 300.0);
            ++doc.touch;
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (Icons::iconButton("click_btn", ImVec2(24.0f, 24.0f), "click", doc.click, "Metronome Click (K)", C_BLUE)) {
            doc.click = !doc.click;
        }
        curX += gw + 5.0f;
    }

    // 8. Stereo Output Level Meter - after toolbar.rs master_meter
    {
        const float gw = 24.0f;
        drawToolbarFrame(dl, ImVec2(curX, groupY), ImVec2(curX + gw, groupY + groupH));
        const float level = Playback::isPlaying() ? 0.68f : 0.04f;
        for (int ch = 0; ch < 2; ++ch) {
            const float bx = curX + 5.0f + ch * 8.0f;
            const float by0 = groupY + 5.0f;
            const float by1 = groupY + groupH - 5.0f;
            const float bh = by1 - by0;
            dl->AddRectFilled(ImVec2(bx, by0), ImVec2(bx + 5.0f, by1), C_LCD, 1.0f);
            const float fillH = bh * level;
            if (fillH > 1.0f) {
                dl->AddRectFilled(ImVec2(bx, by1 - fillH), ImVec2(bx + 5.0f, by1), C_PLAY, 1.0f);
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// 5 Rulers (Bars|Beats, Min:Secs, Tempo, Meter, Markers) - after edit_window/rulers.rs
// ============================================================================

static void drawDiamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill) {
    const ImVec2 pts[4] = {
        ImVec2(c.x, c.y - r),
        ImVec2(c.x + r, c.y),
        ImVec2(c.x, c.y + r),
        ImVec2(c.x - r, c.y),
    };
    dl->AddConvexPolyFilled(pts, 4, fill);
}

static void drawRulers(ImDrawList* dl, const ImVec2& headerOrigin, float laneW, Session::Document& doc) {
    const float x0 = headerOrigin.x + kHeaderW;
    const float y0 = headerOrigin.y;
    const float x1 = x0 + laneW;

    dl->AddRectFilled(headerOrigin, ImVec2(x0, y0 + kRulersH), C_BG1);
    dl->AddLine(ImVec2(x0 - 1.0f, y0), ImVec2(x0 - 1.0f, y0 + kRulersH), C_BORDER);

    struct RulerRowInfo {
        const char* label;
        float h;
        ImU32 bg;
    };
    const RulerRowInfo rows[5] = {
        {"Bars|Beats", kRulerBarsH, C_BG2},
        {"Min:Secs",   kRulerTimeH, C_BG1},
        {"Tempo",      kRulerTempoH, C_BG2},
        {"Meter",      kRulerMeterH, C_BG1},
        {"Markers",    kRulerMarkersH, C_BG2},
    };

    float ry = y0;
    for (int i = 0; i < 5; ++i) {
        dl->AddRectFilled(ImVec2(headerOrigin.x, ry), ImVec2(x0 - 1.0f, ry + rows[i].h), C_BG1);
        dl->AddRectFilled(ImVec2(x0, ry), ImVec2(x1, ry + rows[i].h), rows[i].bg);
        dl->AddText(ImVec2(headerOrigin.x + 8.0f, ry + (rows[i].h - 13.0f) * 0.5f), C_DIM, rows[i].label);
        dl->AddLine(ImVec2(headerOrigin.x, ry + rows[i].h - 1.0f), ImVec2(x1, ry + rows[i].h - 1.0f), C_BORDER);
        if (i == 4) {
            ImGui::SetCursorScreenPos(ImVec2(x0 - 20.0f, ry + 1.0f));
            if (Icons::iconButton("add_marker_ruler", ImVec2(16.0f, 15.0f), "plus", false, "Add Marker at Playhead", C_BLUE)) {
                App::mutateDocument("Add Marker", [](Session::Document& d) {
                    Session::Marker m;
                    m.number = (unsigned)(d.markers.size() + 1);
                    m.id = "m" + std::to_string(m.number);
                    m.name = "Marker " + std::to_string(m.number);
                    m.start = Playback::playhead();
                    d.markers.push_back(std::move(m));
                });
            }
        }
        ry += rows[i].h;
    }

    const double rate = doc.sampleRate > 0 ? doc.sampleRate : 48000.0;
    const double spb = doc.time.samplesPerBeat(rate);
    const long long visStart = (long long)scrollX;
    const long long visEnd = visStart + (long long)(laneW * gSamplesPerPixel);
    const auto sampleToX = [&](long long s) -> float {
        return x0 + (float)((double)(s - visStart) / (double)gSamplesPerPixel);
    };

    dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y0 + kRulersH), true);

    // 1. Bars|Beats ruler
    {
        const float by0 = y0;
        const float by1 = y0 + kRulerBarsH;
        const long long beatSamples = std::max(1LL, (long long)std::llround(spb));
        const int beatsPerBar = std::max(1, doc.time.meter.num);
        const long long barSamples = std::max(1LL, beatSamples * beatsPerBar);
        const float barPx = (float)barSamples / gSamplesPerPixel;
        const int barStride = barPx < 14.0f ? 8 : (barPx < 28.0f ? 4 : (barPx < 52.0f ? 2 : 1));
        const bool drawBeats = (barPx / (float)beatsPerBar) >= 12.0f;

        const long long firstBeat = (visStart / beatSamples) * beatSamples;
        for (long long s = firstBeat; s <= visEnd; s += beatSamples) {
            const float sx = sampleToX(s);
            const long long beatIdx = s / beatSamples;
            const bool isBar = (beatIdx % beatsPerBar) == 0;
            const long long barNum = (beatIdx / beatsPerBar) + 1;
            if (isBar) {
                const bool major = ((barNum - 1) % barStride) == 0;
                dl->AddLine(ImVec2(sx, by0 + (major ? 2.0f : 10.0f)), ImVec2(sx, by1 - 1.0f), major ? C_TEXT : C_MUTED);
                if (major) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%lld", (long long)barNum);
                    dl->AddText(ImVec2(sx + 4.0f, by0 + 3.0f), C_TEXT, buf);
                }
            } else if (drawBeats) {
                dl->AddLine(ImVec2(sx, by1 - 6.0f), ImVec2(sx, by1 - 1.0f), C_MUTED);
            }
        }

        // Playhead triangle on Bars|Beats ruler
        const float headX = sampleToX(Playback::playhead());
        if (headX >= x0 && headX <= x1) {
            const ImVec2 tri[3] = {
                ImVec2(headX - 5.0f, by0 + 1.0f),
                ImVec2(headX + 5.0f, by0 + 1.0f),
                ImVec2(headX, by1 - 2.0f),
            };
            dl->AddTriangleFilled(tri[0], tri[1], tri[2], IM_COL32(0xff, 0xdd, 0x55, 255));
        }

        ImGui::SetCursorScreenPos(ImVec2(x0, y0));
        if (ImGui::InvisibleButton("ruler_locate_btn", ImVec2(laneW, kRulerBarsH + kRulerTimeH))) {
            const float mx = ImGui::GetIO().MousePos.x;
            const long long rawAt = std::max(0LL, visStart + (long long)((mx - x0) * gSamplesPerPixel));
            const long long snapped = EditDrag::dragSnap(doc, rawAt, ImGui::GetIO().KeyCtrl);
            Playback::setPlayhead(snapped);
        }
    }

    // 2. Min:Secs ruler
    {
        const float ty0 = y0 + kRulerBarsH;
        const float ty1 = ty0 + kRulerTimeH;
        const double visSecs = (double)(visEnd - visStart) / rate;
        double stepSec = 1.0;
        for (double cand : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0}) {
            if (visSecs / cand <= 14.0) {
                stepSec = cand;
                break;
            }
        }
        const long long stepSamples = std::max(1LL, (long long)std::llround(stepSec * rate));
        for (long long s = (visStart / stepSamples) * stepSamples; s <= visEnd; s += stepSamples) {
            const float sx = sampleToX(s);
            dl->AddLine(ImVec2(sx, ty1 - 6.0f), ImVec2(sx, ty1 - 1.0f), C_MUTED);
            const double sec = (double)s / rate;
            const int mins = (int)(sec / 60.0);
            const double rem = sec - mins * 60.0;
            char buf[32];
            if (stepSec < 1.0)
                std::snprintf(buf, sizeof(buf), "%d:%05.2f", mins, rem);
            else
                std::snprintf(buf, sizeof(buf), "%d:%02d", mins, (int)std::llround(rem));
            dl->AddText(ImVec2(sx + 3.0f, ty0 + 2.0f), C_DIM, buf);
        }
    }

    // 3. Tempo ruler
    {
        const float tpy0 = y0 + kRulerBarsH + kRulerTimeH;
        const float tpy1 = tpy0 + kRulerTempoH;
        for (size_t i = 0; i < doc.time.tempos.size(); ++i) {
            const auto& tp = doc.time.tempos[i];
            const long long atSample = (long long)std::llround((double)tp.tick * (spb / 960.0));
            const float sx = sampleToX(atSample);
            if (sx < x0 - 40.0f || sx > x1 + 40.0f) continue;
            drawDiamond(dl, ImVec2(sx, (tpy0 + tpy1) * 0.5f), 4.0f, IM_COL32(0x66, 0xd9, 0x8a, 255));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", tp.bpm);
            dl->AddText(ImVec2(sx + 6.0f, tpy0 + 1.0f), IM_COL32(0x66, 0xd9, 0x8a, 255), buf);
        }
        if (doc.time.tempos.empty()) {
            const float sx = sampleToX(0);
            drawDiamond(dl, ImVec2(sx, (tpy0 + tpy1) * 0.5f), 4.0f, IM_COL32(0x66, 0xd9, 0x8a, 255));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", doc.time.tempoBpm);
            dl->AddText(ImVec2(sx + 6.0f, tpy0 + 1.0f), IM_COL32(0x66, 0xd9, 0x8a, 255), buf);
        }
        ImGui::SetCursorScreenPos(ImVec2(x0, tpy0));
        ImGui::InvisibleButton("tempo_ruler_hit", ImVec2(laneW, kRulerTempoH));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const float mx = ImGui::GetIO().MousePos.x;
            gRenameModal.open = true;
            gRenameModal.kind = 2;
            gRenameModal.sampleAt = std::max(0LL, visStart + (long long)((mx - x0) * gSamplesPerPixel));
            gRenameModal.bpm = (float)doc.time.tempoBpm;
        }
    }

    // 4. Meter ruler
    {
        const float my0 = y0 + kRulerBarsH + kRulerTimeH + kRulerTempoH;
        const float my1 = my0 + kRulerMeterH;
        const float sx = sampleToX(0);
        drawDiamond(dl, ImVec2(sx, (my0 + my1) * 0.5f), 4.0f, IM_COL32(0xc4, 0x8a, 0xff, 255));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d/%d", doc.time.meter.num, doc.time.meter.den);
        dl->AddText(ImVec2(sx + 6.0f, my0 + 1.0f), IM_COL32(0xc4, 0x8a, 0xff, 255), buf);
    }

    // 5. Markers ruler
    {
        const float mky0 = y0 + kRulerBarsH + kRulerTimeH + kRulerTempoH + kRulerMeterH;
        const float mky1 = mky0 + kRulerMarkersH;
        const ImU32 mCol = IM_COL32(0xff, 0xc8, 0x42, 255);
        for (size_t mi = 0; mi < doc.markers.size(); ++mi) {
            const auto& m = doc.markers[mi];
            const float sx = sampleToX(m.start);
            if (sx < x0 - 80.0f || sx > x1 + 40.0f) continue;
            if (m.end > m.start) {
                const float ex = sampleToX(m.end);
                dl->AddRectFilled(ImVec2(sx, mky0 + 2.0f), ImVec2(ex, mky1 - 2.0f),
                                  IM_COL32(0xff, 0xc8, 0x42, 65), 2.0f);
            }
            drawDiamond(dl, ImVec2(sx, (mky0 + mky1) * 0.5f), 4.5f, mCol);
            dl->AddText(ImVec2(sx + 7.0f, mky0 + 2.0f), mCol, m.name.c_str());

            ImGui::SetCursorScreenPos(ImVec2(std::max(x0, sx - 5.0f), mky0));
            char hitId[32];
            std::snprintf(hitId, sizeof(hitId), "mk_hit_%zu", mi);
            if (ImGui::InvisibleButton(hitId, ImVec2(64.0f, kRulerMarkersH))) {
                Playback::setPlayhead(m.start);
                if (m.end > m.start) {
                    Editor::selectRange(m.start, m.end, Editor::selection().trackId);
                    doc.sessionEdit.selection = Time::Range{m.start, m.end};
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                gRenameModal.open = true;
                gRenameModal.kind = 1;
                gRenameModal.targetId = m.id;
                std::snprintf(gRenameModal.nameBuf, sizeof(gRenameModal.nameBuf), "%s", m.name.c_str());
            }
            if (ImGui::BeginPopupContextItem(hitId)) {
                if (ImGui::MenuItem("Rename Marker...")) {
                    gRenameModal.open = true;
                    gRenameModal.kind = 1;
                    gRenameModal.targetId = m.id;
                    std::snprintf(gRenameModal.nameBuf, sizeof(gRenameModal.nameBuf), "%s", m.name.c_str());
                }
                if (ImGui::MenuItem("Delete Marker")) {
                    const std::string delId = m.id;
                    App::mutateDocument("Delete Marker", [delId](Session::Document& d) {
                        d.markers.erase(std::remove_if(d.markers.begin(), d.markers.end(),
                                                       [&](const Session::Marker& x) { return x.id == delId; }),
                                        d.markers.end());
                    });
                }
                ImGui::EndPopup();
            }
        }
    }

    dl->PopClipRect();
}

// ============================================================================
// Track Header (after SoundCraft crates/ui-egui/src/edit_window/header.rs)
// ============================================================================

static const char* trackKindBadge(Session::TrackKind kind) {
    switch (kind) {
        case Session::TrackKind::Audio: return nullptr;
        case Session::TrackKind::Instrument: return "INST";
        case Session::TrackKind::Midi: return "MIDI";
        case Session::TrackKind::Aux: return "AUX";
        case Session::TrackKind::Master: return "MASTER";
        case Session::TrackKind::Vca: return "VCA";
        case Session::TrackKind::Folder: return "FOLDER";
        case Session::TrackKind::Video: return "VIDEO";
    }
    return nullptr;
}

static bool drawSmallToggleKey(const char* id, const char* letter, bool on, ImU32 onCol, ImVec2 pos, const char* tooltip) {
    ImGui::SetCursorScreenPos(pos);
    const ImVec2 sz(17.0f, 17.0f);
    const bool clicked = ImGui::InvisibleButton(id, sz);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 br(pos.x + sz.x, pos.y + sz.y);
    dl->AddRectFilled(pos, br, on ? onCol : (hovered ? C_BG4 : C_BG3), 2.0f);
    dl->AddRect(pos, br, C_BORDER, 2.0f);
    const ImVec2 ts = ImGui::CalcTextSize(letter);
    dl->AddText(ImVec2(pos.x + (sz.x - ts.x) * 0.5f, pos.y + (sz.y - ts.y) * 0.5f),
                on ? IM_COL32(15, 15, 15, 255) : C_DIM, letter);
    return clicked;
}

static void drawTrackHeader(ImDrawList* dl, Session::Document& doc, int ti, float hx, float hy, float rowH) {
    Session::Track& track = doc.tracks[(size_t)ti];
    const bool isSelected = std::find(doc.sessionEdit.selectedTracks.begin(), doc.sessionEdit.selectedTracks.end(), track.numId) != doc.sessionEdit.selectedTracks.end()
                         || Editor::selection().trackId == track.id;

    dl->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + kHeaderW, hy + rowH), isSelected ? C_BG3 : C_BG2);
    dl->AddLine(ImVec2(hx, hy + rowH - 1.0f), ImVec2(hx + kHeaderW, hy + rowH - 1.0f), C_BORDER);
    dl->AddLine(ImVec2(hx + kHeaderW - 1.0f, hy), ImVec2(hx + kHeaderW - 1.0f, hy + rowH), C_BORDER);

    // Left 12px colour strip
    const ImU32 trackCol = track.inactive
        ? IM_COL32(track.color[0] / 2, track.color[1] / 2, track.color[2] / 2, 255)
        : (isSelected ? IM_COL32(std::min(255, (int)track.color[0] + 35), std::min(255, (int)track.color[1] + 35), std::min(255, (int)track.color[2] + 35), 255)
                      : IM_COL32(track.color[0], track.color[1], track.color[2], 255));
    dl->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + 12.0f, hy + rowH - 1.0f), trackCol);

    // Row 1: Name plate + playlist menu + kind badge + R / I / S / M buttons
    const bool hasRI = track.kind == Session::TrackKind::Audio || track.kind == Session::TrackKind::Instrument || track.kind == Session::TrackKind::Midi;
    const bool isMaster = track.kind == Session::TrackKind::Master;
    const char* badge = trackKindBadge(track.kind);

    float rightButtonsW = isMaster ? 0.0f : (hasRI ? 4 * 18.0f : 2 * 18.0f);
    const float badgeW = badge ? (ImGui::CalcTextSize(badge).x + 8.0f) : 0.0f;
    const float playlistBtnW = (track.kind == Session::TrackKind::Audio) ? 14.0f : 0.0f;
    const float namePlateW = std::max(44.0f, kHeaderW - 16.0f - 14.0f - rightButtonsW - badgeW - playlistBtnW - 8.0f);

    const ImVec2 npMin(hx + 16.0f, hy + 4.0f);
    const ImVec2 npMax(npMin.x + namePlateW, hy + 21.0f);
    dl->AddRectFilled(npMin, npMax, isSelected ? C_BG1 : IM_COL32(0x18, 0x18, 0x18, 255), 2.0f);
    dl->AddRect(npMin, npMax, isSelected ? C_BLUE : C_BORDER, 2.0f);
    dl->PushClipRect(ImVec2(npMin.x + 3.0f, npMin.y), ImVec2(npMax.x - 3.0f, npMax.y), true);
    dl->AddText(ImVec2(npMin.x + 4.0f, npMin.y + 2.0f), track.inactive ? C_MUTED : C_TEXT, track.name.c_str());
    dl->PopClipRect();

    ImGui::SetCursorScreenPos(npMin);
    char npId[32];
    std::snprintf(npId, sizeof(npId), "tr_np_%d", ti);
    if (ImGui::InvisibleButton(npId, ImVec2(namePlateW, 17.0f))) {
        if (!ImGui::GetIO().KeyShift) doc.sessionEdit.selectedTracks.clear();
        if (std::find(doc.sessionEdit.selectedTracks.begin(), doc.sessionEdit.selectedTracks.end(), track.numId) == doc.sessionEdit.selectedTracks.end()) {
            doc.sessionEdit.selectedTracks.push_back(track.numId);
        }
        Editor::selectRange(Editor::selection().start, Editor::selection().end, track.id);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        gRenameModal.open = true;
        gRenameModal.kind = 0;
        gRenameModal.targetId = track.id;
        gRenameModal.targetNumId = track.numId;
        std::snprintf(gRenameModal.nameBuf, sizeof(gRenameModal.nameBuf), "%s", track.name.c_str());
    }

    char ctxId[32];
    std::snprintf(ctxId, sizeof(ctxId), "tr_ctx_%d", ti);
    if (ImGui::BeginPopupContextItem(ctxId)) {
        if (ImGui::MenuItem("Rename...")) {
            gRenameModal.open = true;
            gRenameModal.kind = 0;
            gRenameModal.targetId = track.id;
            gRenameModal.targetNumId = track.numId;
            std::snprintf(gRenameModal.nameBuf, sizeof(gRenameModal.nameBuf), "%s", track.name.c_str());
        }
        if (ImGui::MenuItem("Duplicate Track")) {
            const uint64_t tid = track.numId;
            App::mutateDocument("Duplicate Track", [tid](Session::Document& d) {
                EngCmd::Engine e;
                e.doc = std::move(d);
                EngCmd::Engine::Params p;
                p.nums["track"] = (double)tid;
                e.execute("track.duplicate", p);
                d = std::move(e.doc);
            });
        }
        if (ImGui::MenuItem(track.frozen ? "Unfreeze Track" : "Freeze Track")) {
            track.frozen = !track.frozen;
        }
        if (ImGui::MenuItem(track.inactive ? "Make Active" : "Hide and Make Inactive")) {
            track.inactive = !track.inactive;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Track")) {
            const std::string delId = track.id;
            App::mutateDocument("Delete Track", [delId](Session::Document& d) {
                d.tracks.erase(std::remove_if(d.tracks.begin(), d.tracks.end(),
                                              [&](const Session::Track& x) { return x.id == delId; }),
                               d.tracks.end());
            });
            ImGui::EndPopup();
            return;
        }
        ImGui::EndPopup();
    }

    float rx = npMax.x + 2.0f;
    if (playlistBtnW > 0.0f) {
        ImGui::SetCursorScreenPos(ImVec2(rx, hy + 4.0f));
        char plBtnId[32], plPopId[32];
        std::snprintf(plBtnId, sizeof(plBtnId), "pl_btn_%d", ti);
        std::snprintf(plPopId, sizeof(plPopId), "pl_pop_%d", ti);
        if (Icons::iconButton(plBtnId, ImVec2(13.0f, 17.0f), "triangle_down", false, "Track Playlists", C_BLUE)) {
            ImGui::OpenPopup(plPopId);
        }
        if (ImGui::BeginPopup(plPopId)) {
            if (ImGui::MenuItem("New Playlist")) {
                const uint64_t tid = track.numId;
                App::mutateDocument("New Playlist", [tid](Session::Document& d) {
                    EngCmd::Engine e;
                    e.doc = std::move(d);
                    EngCmd::Engine::Params p;
                    p.nums["track"] = (double)tid;
                    e.execute("track.playlist_new", p);
                    d = std::move(e.doc);
                });
            }
            if (ImGui::MenuItem("Duplicate Playlist")) {
                const uint64_t tid = track.numId;
                App::mutateDocument("Duplicate Playlist", [tid](Session::Document& d) {
                    EngCmd::Engine e;
                    e.doc = std::move(d);
                    EngCmd::Engine::Params p;
                    p.nums["track"] = (double)tid;
                    e.execute("track.playlist_duplicate", p);
                    d = std::move(e.doc);
                });
            }
            if (!track.playlists.empty()) {
                ImGui::Separator();
                for (size_t pi = 0; pi < track.playlists.size(); ++pi) {
                    if (ImGui::MenuItem(track.playlists[pi].name.c_str(), nullptr, track.activePlaylist == pi)) {
                        const uint64_t tid = track.numId;
                        const size_t targetPi = pi;
                        App::mutateDocument("Switch Playlist", [tid, targetPi](Session::Document& d) {
                            EngCmd::Engine e;
                            e.doc = std::move(d);
                            EngCmd::Engine::Params p;
                            p.nums["track"] = (double)tid;
                            p.nums["index"] = (double)targetPi;
                            e.execute("track.playlist_select", p);
                            d = std::move(e.doc);
                        });
                    }
                }
            }
            ImGui::EndPopup();
        }
        rx += 15.0f;
    }

    if (badge) {
        const ImVec2 bMin(rx, hy + 5.0f);
        const ImVec2 bMax(rx + badgeW - 2.0f, hy + 20.0f);
        dl->AddRectFilled(bMin, bMax, C_BG3, 2.0f);
        dl->AddText(ImVec2(bMin.x + 3.0f, bMin.y + 1.0f), C_DIM, badge);
    }

    // R / I / S / M buttons on right side of Row 1
    if (!isMaster) {
        float bx = hx + kHeaderW - 14.0f - rightButtonsW;
        char bid[32];
        if (hasRI) {
            std::snprintf(bid, sizeof(bid), "tr_r_%d", ti);
            if (drawSmallToggleKey(bid, "R", track.recordArm, C_REC, ImVec2(bx, hy + 4.0f), "Record Enable")) {
                track.recordArm = !track.recordArm;
            }
            bx += 18.0f;
            std::snprintf(bid, sizeof(bid), "tr_i_%d", ti);
            if (drawSmallToggleKey(bid, "I", track.inputMonitor, C_PLAY, ImVec2(bx, hy + 4.0f), "Track Input Monitor")) {
                track.inputMonitor = !track.inputMonitor;
            }
            bx += 18.0f;
        }
        std::snprintf(bid, sizeof(bid), "tr_s_%d", ti);
        if (drawSmallToggleKey(bid, "S", track.solo, C_SOLO, ImVec2(bx, hy + 4.0f), "Solo")) {
            track.solo = !track.solo;
            ++doc.touch;
        }
        bx += 18.0f;
        std::snprintf(bid, sizeof(bid), "tr_m_%d", ti);
        if (drawSmallToggleKey(bid, "M", track.mute, C_MUTE, ImVec2(bx, hy + 4.0f), "Mute")) {
            track.mute = !track.mute;
            ++doc.touch;
        }
    }

    // Row 2: Track View selector & Automation Mode selector (when rowH >= 44)
    if (rowH >= 44.0f) {
        auto drawSmallDropdown = [&](const char* btnId, const char* popId, const char* text, ImVec2 pos, float w) {
            const ImVec2 br(pos.x + w, pos.y + 16.0f);
            dl->AddRectFilled(pos, br, C_BG3, 2.0f);
            dl->AddRect(pos, br, C_BORDER, 2.0f);
            dl->PushClipRect(ImVec2(pos.x + 3.0f, pos.y), ImVec2(br.x - 11.0f, br.y), true);
            dl->AddText(ImVec2(pos.x + 4.0f, pos.y + 1.0f), C_DIM, text);
            dl->PopClipRect();
            Icons::draw(dl, ImVec2(br.x - 9.0f, pos.y + 5.0f), ImVec2(br.x - 3.0f, pos.y + 11.0f), "triangle_down", C_DIM);
            ImGui::SetCursorScreenPos(pos);
            if (ImGui::InvisibleButton(btnId, ImVec2(w, 16.0f))) {
                ImGui::OpenPopup(popId);
            }
        };

        const char* curView = track.view.empty()
            ? (Session::isMidiKind(track.kind) ? "notes" : (track.kind == Session::TrackKind::Aux || track.kind == Session::TrackKind::Master ? "volume" : "waveform"))
            : track.view.c_str();

        char vBtnId[32], vPopId[32];
        std::snprintf(vBtnId, sizeof(vBtnId), "tv_btn_%d", ti);
        std::snprintf(vPopId, sizeof(vPopId), "tv_pop_%d", ti);
        drawSmallDropdown(vBtnId, vPopId, curView, ImVec2(hx + 16.0f, hy + 25.0f), 84.0f);
        if (ImGui::BeginPopup(vPopId)) {
            static const char* kViews[] = {"waveform", "blocks", "notes", "velocity", "volume", "pan", "mute"};
            for (const char* v : kViews) {
                if (ImGui::MenuItem(v, nullptr, std::strcmp(curView, v) == 0)) {
                    track.view = v;
                }
            }
            ImGui::EndPopup();
        }

        const char* curAuto = track.automationMode.empty() ? "read" : track.automationMode.c_str();
        char aBtnId[32], aPopId[32];
        std::snprintf(aBtnId, sizeof(aBtnId), "ta_btn_%d", ti);
        std::snprintf(aPopId, sizeof(aPopId), "ta_pop_%d", ti);
        drawSmallDropdown(aBtnId, aPopId, curAuto, ImVec2(hx + 104.0f, hy + 25.0f), 80.0f);
        if (ImGui::BeginPopup(aPopId)) {
            static const char* kModes[] = {"off", "read", "touch", "latch", "write"};
            for (const char* m : kModes) {
                if (ImGui::MenuItem(m, nullptr, std::strcmp(curAuto, m) == 0)) {
                    track.automationMode = m;
                }
            }
            ImGui::EndPopup();
        }
    }

    // Row 3: Volume & Pan readouts (when rowH >= 64)
    if (rowH >= 64.0f) {
        const float gainDb = track.gainDb;
        char volStr[32];
        if (gainDb <= -59.9f) std::snprintf(volStr, sizeof(volStr), "-inf dB");
        else std::snprintf(volStr, sizeof(volStr), "%+.1f dB", gainDb);

        dl->AddText(ImVec2(hx + 16.0f, hy + 45.0f), C_MUTED, "vol");
        const ImVec2 vBoxMin(hx + 36.0f, hy + 44.0f);
        const ImVec2 vBoxMax(hx + 100.0f, hy + 59.0f);
        dl->AddRectFilled(vBoxMin, vBoxMax, C_LCD, 2.0f);
        dl->AddRect(vBoxMin, vBoxMax, C_BORDER, 2.0f);
        dl->AddText(ImVec2(vBoxMin.x + 4.0f, vBoxMin.y + 1.0f), C_TEXT, volStr);

        ImGui::SetCursorScreenPos(vBoxMin);
        char vDragId[32];
        std::snprintf(vDragId, sizeof(vDragId), "tr_vol_%d", ti);
        ImGui::InvisibleButton(vDragId, ImVec2(vBoxMax.x - vBoxMin.x, vBoxMax.y - vBoxMin.y));
        if (ImGui::IsItemActivated()) {
            gGesture.kind = GestureKind::DragVol;
            gGesture.trackIdx = ti;
            gGesture.startMouseY = ImGui::GetIO().MousePos.y;
            gGesture.startVal = gainDb;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            track.gainDb = 0.0f;
            ++doc.touch;
        }

        if (!isMaster) {
            dl->AddText(ImVec2(hx + 106.0f, hy + 45.0f), C_MUTED, "pan");
            char panStr[32];
            const int panPct = (int)std::llround(track.pan * 100.0f);
            if (panPct == 0) std::snprintf(panStr, sizeof(panStr), "C");
            else if (panPct < 0) std::snprintf(panStr, sizeof(panStr), "L%d", -panPct);
            else std::snprintf(panStr, sizeof(panStr), "R%d", panPct);

            const ImVec2 pBoxMin(hx + 128.0f, hy + 44.0f);
            const ImVec2 pBoxMax(hx + 184.0f, hy + 59.0f);
            dl->AddRectFilled(pBoxMin, pBoxMax, C_LCD, 2.0f);
            dl->AddRect(pBoxMin, pBoxMax, C_BORDER, 2.0f);
            dl->AddText(ImVec2(pBoxMin.x + 6.0f, pBoxMin.y + 1.0f), C_TEXT, panStr);

            ImGui::SetCursorScreenPos(pBoxMin);
            char pDragId[32];
            std::snprintf(pDragId, sizeof(pDragId), "tr_pan_%d", ti);
            ImGui::InvisibleButton(pDragId, ImVec2(pBoxMax.x - pBoxMin.x, pBoxMax.y - pBoxMin.y));
            if (ImGui::IsItemActivated()) {
                gGesture.kind = GestureKind::DragPan;
                gGesture.trackIdx = ti;
                gGesture.startMouseX = ImGui::GetIO().MousePos.x;
                gGesture.startVal = track.pan;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                track.pan = 0.0f;
                ++doc.touch;
            }
        }
    }

    // Row 4: Inserts summary (when rowH >= 78)
    if (rowH >= 78.0f) {
        const char* insText = track.insertsSummary.empty() ? "\xe2\x80\x94 no inserts \xe2\x80\x94" : track.insertsSummary.c_str();
        const ImU32 insCol = track.insertsSummary.empty() ? C_MUTED : C_DIM;
        dl->PushClipRect(ImVec2(hx + 16.0f, hy + 62.0f), ImVec2(hx + kHeaderW - 14.0f, hy + rowH - 2.0f), true);
        dl->AddText(ImVec2(hx + 16.0f, hy + 64.0f), insCol, insText);
        dl->PopClipRect();
    }

    // Right-edge vertical track level meter
    {
        const float mx0 = hx + kHeaderW - 9.0f;
        const float my0 = hy + 4.0f;
        const float my1 = hy + rowH - 5.0f;
        dl->AddRectFilled(ImVec2(mx0, my0), ImVec2(mx0 + 5.0f, my1), C_LCD, 1.0f);
        float lvl = 0.0f;
        if (!track.mute && !track.inactive) {
            const float lin = std::pow(10.0f, track.gainDb / 20.0f);
            lvl = Playback::isPlaying() ? std::clamp(lin * 0.65f, 0.0f, 1.0f) : 0.0f;
        }
        if (lvl > 0.01f) {
            const float fh = (my1 - my0) * lvl;
            dl->AddRectFilled(ImVec2(mx0, my1 - fh), ImVec2(mx0 + 5.0f, my1), C_PLAY, 1.0f);
        }
    }

    // Bottom edge resize handle
    ImGui::SetCursorScreenPos(ImVec2(hx, hy + rowH - 4.0f));
    char rszId[32];
    std::snprintf(rszId, sizeof(rszId), "tr_rsz_%d", ti);
    ImGui::InvisibleButton(rszId, ImVec2(kHeaderW, 6.0f));
    if (ImGui::IsItemHovered() || (gGesture.kind == GestureKind::ResizeTrack && gGesture.trackIdx == ti)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActivated()) {
        gGesture.kind = GestureKind::ResizeTrack;
        gGesture.trackIdx = ti;
        gGesture.startMouseY = ImGui::GetIO().MousePos.y;
        gGesture.startVal = trackHeightPx(track);
    }
}

// ============================================================================
// Clip, Waveform, Fade, MIDI & Automation Lane Drawing
// ============================================================================

static void drawWaveform(ImDrawList* dl, const ImVec2& tl, const ImVec2& br, const Mix::Peaks& peaks, float wfScale, ImU32 lineCol, ImU32 fillCol) {
    if (peaks.buckets <= 0 || peaks.minMax.empty()) return;
    const float w = br.x - tl.x;
    const float h = br.y - tl.y;
    const float mid = (tl.y + br.y) * 0.5f;
    const int buckets = peaks.buckets;
    dl->AddLine(ImVec2(tl.x, mid), ImVec2(br.x, mid), IM_COL32(255, 255, 255, 25));
    for (int b = 0; b < buckets; ++b) {
        const float xc = tl.x + w * ((float)b + 0.5f) / (float)buckets;
        const float mn = std::clamp(peaks.minMax[(size_t)b * 2] * wfScale, -1.0f, 1.0f);
        const float mx = std::clamp(peaks.minMax[(size_t)b * 2 + 1] * wfScale, -1.0f, 1.0f);
        const float yTop = mid - mx * h * 0.46f;
        const float yBot = mid - mn * h * 0.46f;
        dl->AddLine(ImVec2(xc, yTop), ImVec2(xc, yBot), (b & 1) ? lineCol : fillCol, 1.0f);
    }
}

static void drawFadeOverlay(ImDrawList* dl, float x0, float x1, float y0, float y1, const Session::Fade& fade, bool isFadeIn) {
    if (fade.len <= 0 || x1 <= x0 + 1.0f) return;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 0, 0, 75));
    const int steps = std::clamp((int)((x1 - x0) / 3.0f), 4, 48);
    ImVec2 prev(x0, isFadeIn ? y1 : y0);
    for (int i = 1; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        const float pos = isFadeIn ? t : (1.0f - t);
        const float g = ClipOps::fadeGain(fade, pos);
        const float px = x0 + (x1 - x0) * t;
        const float py = y1 - (y1 - y0) * std::clamp(g, 0.0f, 1.0f);
        dl->AddLine(prev, ImVec2(px, py), IM_COL32(255, 255, 255, 210), 1.5f);
        prev = ImVec2(px, py);
    }
}

static void drawMidiClipNotes(ImDrawList* dl, const Session::Clip& clip, const Session::Document& doc,
                              float clipX0, float clipX1, float bodyY0, float bodyY1, ImU32 noteCol) {
    if (!clip.midi.has_value() || clip.midi->notes.empty()) return;
    const auto& notes = clip.midi->notes;
    uint8_t minPitch = 127, maxPitch = 0;
    for (const auto& n : notes) {
        minPitch = std::min(minPitch, n.pitch);
        maxPitch = std::max(maxPitch, n.pitch);
    }
    const int lo = std::max(0, (int)minPitch - 3);
    const int hi = std::min(127, (int)maxPitch + 3);
    const int span = std::max(8, hi - lo + 1);
    const float bodyH = std::max(8.0f, bodyY1 - bodyY0);
    const float noteH = std::clamp(bodyH / (float)span, 3.0f, 8.5f);
    const double spb = doc.time.samplesPerBeat(doc.sampleRate > 0 ? doc.sampleRate : 48000.0);
    const double samplesPerTick = spb / 960.0;

    dl->PushClipRect(ImVec2(clipX0 + 1.0f, bodyY0), ImVec2(clipX1 - 1.0f, bodyY1), true);
    for (const auto& n : notes) {
        const double startSamples = (double)n.start * samplesPerTick - (double)clip.offset;
        const double lenSamples = std::max(1.0, (double)n.length * samplesPerTick);
        const float nx0 = clipX0 + (float)(startSamples / (double)gSamplesPerPixel);
        const float nx1 = std::max(nx0 + 3.0f, nx0 + (float)(lenSamples / (double)gSamplesPerPixel));
        if (nx1 < clipX0 || nx0 > clipX1) continue;
        const float normP = (float)((int)n.pitch - lo) / (float)std::max(1, span - 1);
        const float ny = bodyY1 - 2.0f - normP * (bodyH - noteH - 4.0f) - noteH;
        dl->AddRectFilled(ImVec2(nx0, ny), ImVec2(nx1, ny + noteH), noteCol, 1.5f);
        dl->AddRect(ImVec2(nx0, ny), ImVec2(nx1, ny + noteH), IM_COL32(0, 0, 0, 160), 1.5f);
    }
    dl->PopClipRect();
}

static void drawAutomationLane(ImDrawList* dl, Session::Document& doc, Session::Track& track,
                               float laneX0, float laneX1, float rowY, float rowH, long long visStart) {
    Session::AutoParamKind kind = Session::AutoParamKind::Volume;
    ImU32 lineCol = IM_COL32(0x66, 0xbb, 0xff, 255);
    float defVal = 0.0f, minV = -24.0f, maxV = 12.0f;
    if (track.view == "pan") {
        kind = Session::AutoParamKind::Pan;
        lineCol = IM_COL32(0xff, 0xaa, 0x44, 255);
        defVal = 0.0f;
        minV = -1.0f;
        maxV = 1.0f;
    } else if (track.view == "mute") {
        kind = Session::AutoParamKind::Mute;
        lineCol = IM_COL32(0xff, 0x66, 0x66, 255);
        defVal = 0.0f;
        minV = 0.0f;
        maxV = 1.0f;
    }

    const Session::AutomationLane* lane = nullptr;
    for (const auto& l : track.automation) {
        if (l.param.kind == kind) {
            lane = &l;
            break;
        }
    }

    const float padY = 6.0f;
    const float yTop = rowY + padY;
    const float yBot = rowY + rowH - padY;
    const float h = std::max(8.0f, yBot - yTop);
    const auto valToY = [&](float v) -> float {
        const float norm = std::clamp((v - minV) / (maxV - minV), 0.0f, 1.0f);
        return yBot - norm * h;
    };
    const auto sampleToX = [&](long long s) -> float {
        return laneX0 + (float)((double)(s - visStart) / (double)gSamplesPerPixel);
    };

    const float refY = valToY(defVal);
    dl->AddLine(ImVec2(laneX0, refY), ImVec2(laneX1, refY), IM_COL32(255, 255, 255, 30));

    if (!lane || lane->points.empty()) {
        dl->AddLine(ImVec2(laneX0, refY), ImVec2(laneX1, refY), lineCol, 1.5f);
        return;
    }

    dl->PushClipRect(ImVec2(laneX0, rowY), ImVec2(laneX1, rowY + rowH), true);
    const auto& pts = lane->points;
    const float firstX = std::max(laneX0, sampleToX(pts.front().at));
    const float firstY = valToY(pts.front().value);
    if (firstX > laneX0) {
        dl->AddLine(ImVec2(laneX0, firstY), ImVec2(firstX, firstY), lineCol, 1.8f);
    }
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const float xA = sampleToX(pts[i].at);
        const float yA = valToY(pts[i].value);
        const float xB = sampleToX(pts[i + 1].at);
        const float yB = valToY(pts[i + 1].value);
        if (kind == Session::AutoParamKind::Mute) {
            dl->AddLine(ImVec2(xA, yA), ImVec2(xB, yA), lineCol, 1.8f);
            dl->AddLine(ImVec2(xB, yA), ImVec2(xB, yB), lineCol, 1.8f);
        } else {
            dl->AddLine(ImVec2(xA, yA), ImVec2(xB, yB), lineCol, 1.8f);
        }
    }
    const float lastX = sampleToX(pts.back().at);
    const float lastY = valToY(pts.back().value);
    if (lastX < laneX1) {
        dl->AddLine(ImVec2(lastX, lastY), ImVec2(laneX1, lastY), lineCol, 1.8f);
    }
    for (const auto& p : pts) {
        const float px = sampleToX(p.at);
        const float py = valToY(p.value);
        if (px >= laneX0 - 4.0f && px <= laneX1 + 4.0f) {
            dl->AddCircleFilled(ImVec2(px, py), 3.5f, lineCol);
            dl->AddCircle(ImVec2(px, py), 3.5f, C_BG0, 0, 1.0f);
        }
    }
    dl->PopClipRect();
    (void)doc;
}

// ============================================================================
// Main Timeline Draw (Edit::draw)
// ============================================================================

void draw(Session::Document& doc, float bottomReserve) {
    ImGui::BeginChild("timeline", ImVec2(0, -bottomReserve), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float laneW = std::max(10.0f, avail.x - kHeaderW);
    const auto plan = Mix::currentPlan();
    const float tracksH = totalTracksHeight(doc);

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    gTimelineMin = p0;
    gTimelineMax = ImVec2(p0.x + avail.x, p0.y + avail.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(p0, ImVec2(p0.x + kHeaderW, p0.y + avail.y), C_BG1);
    dl->AddRectFilled(ImVec2(p0.x + kHeaderW, p0.y), ImVec2(p0.x + avail.x, p0.y + avail.y), C_BG0);

    drawRulers(dl, p0, laneW, doc);

    const float rulerTop = p0.y + kRulersH - scrollY;
    gRulerTopY = rulerTop;
    const long long visStart = (long long)scrollX;
    const long long visEnd = visStart + (long long)(laneW * gSamplesPerPixel);

    ImGui::SetCursorScreenPos(ImVec2(p0.x + kHeaderW, p0.y + kRulersH));
    ImGui::InvisibleButton("timeline_lanes_canvas", ImVec2(laneW, std::max(10.0f, avail.y - kRulersH - 14.0f)));
    const bool lanesHovered = ImGui::IsItemHovered();

    if (lanesHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        scrollX = std::max(0.0f, scrollX - ImGui::GetIO().MouseDelta.x * gSamplesPerPixel);
        scrollY = std::max(0.0f, scrollY - ImGui::GetIO().MouseDelta.y);
    }
    if (lanesHovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (ImGui::GetIO().KeyShift) {
                scrollX = std::max(0.0f, scrollX - wheel * gSamplesPerPixel * 48.0f);
            } else if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyAlt) {
                gSamplesPerPixel = std::clamp(gSamplesPerPixel * (wheel > 0 ? 0.85f : 1.18f), 1.0f, 100000.0f);
            } else {
                scrollY = std::clamp(scrollY - wheel * 28.0f, 0.0f, std::max(0.0f, tracksH - (avail.y - kRulersH - 24.0f)));
            }
        }
    }

    // Drag & Drop from Clips Panel onto track lanes or empty space
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* hoverPayload = ImGui::AcceptDragDropPayload(
            ClipsPanel::kDragSourcePayload,
            ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (hoverPayload && hoverPayload->DataSize == sizeof(uint64_t)) {
            const float mx = ImGui::GetIO().MousePos.x;
            const float my = ImGui::GetIO().MousePos.y;
            const int row = trackRowAt(doc, my, rulerTop);
            if (row >= 0) {
                const float ry = rulerTop + trackTopOffset(doc, row);
                const float rh = trackHeightPx(doc.tracks[(size_t)row]);
                dl->AddRect(ImVec2(p0.x + kHeaderW, ry), ImVec2(p0.x + avail.x, ry + rh - 1.0f), C_BLUE, 0.0f, 0, 2.0f);
            } else {
                const float ry = rulerTop + tracksH;
                dl->AddRect(ImVec2(p0.x + kHeaderW, ry), ImVec2(p0.x + avail.x, ry + 72.0f), C_BLUE, 0.0f, 0, 2.0f);
            }
            if (hoverPayload->IsDelivery()) {
                const uint64_t srcNumId = *static_cast<const uint64_t*>(hoverPayload->Data);
                const long long rawAt = std::max(0LL, visStart + (long long)((mx - (p0.x + kHeaderW)) * gSamplesPerPixel));
                const long long at = EditDrag::dragSnap(doc, rawAt, ImGui::GetIO().KeyCtrl);
                const uint64_t trNumId = (row >= 0 && doc.tracks[(size_t)row].kind == Session::TrackKind::Audio)
                                             ? doc.tracks[(size_t)row].numId
                                             : 0;
                auto spot = Spot::source(doc, srcNumId, trNumId, at);
                EditDrag::spotOrRun(doc, std::move(spot));
            }
        }
        ImGui::EndDragDropTarget();
    }

    driveGesture(doc, p0.x, kHeaderW, rulerTop);

    // Mouse click on track lanes
    if (lanesHovered && gGesture.kind == GestureKind::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float mx = ImGui::GetIO().MousePos.x;
        const float my = ImGui::GetIO().MousePos.y;
        const long long atOrigin = std::max(0LL, visStart + (long long)((mx - (p0.x + kHeaderW)) * gSamplesPerPixel));
        const int row = trackRowAt(doc, my, rulerTop);
        if (row >= 0) {
            const auto& track = doc.tracks[(size_t)row];
            const float rowTop = rulerTop + trackTopOffset(doc, row);
            const float rowH = trackHeightPx(track);
            const float laneMid = rowTop + rowH * 0.5f;
            const Editor::Tool tool = Editor::tool();
            const Session::Clip* hit = clipAt(track, atOrigin);
            float x0 = 0, x1 = 0;
            if (hit) {
                x0 = p0.x + kHeaderW + (float)((hit->start - visStart) / gSamplesPerPixel);
                x1 = p0.x + kHeaderW + (float)((ClipOps::end(*hit) - visStart) / gSamplesPerPixel);
            }
            const Editor::Tool eff = effectiveTool(tool, hit, laneMid, my, mx, x0, x1);
            if (eff == Editor::Tool::Zoom) {
                gSamplesPerPixel = std::clamp(gSamplesPerPixel * (ImGui::GetIO().KeyAlt ? 1.5f : 0.65f), 1.0f, 100000.0f);
            } else if (eff == Editor::Tool::Scrubber) {
                Playback::setPlayhead(atOrigin);
            } else if (eff == Editor::Tool::Selector || (eff == Editor::Tool::Smart && !hit)) {
                gGesture.kind = GestureKind::Select;
                gGesture.anchor = EditDrag::dragSnap(doc, atOrigin, ImGui::GetIO().KeyCtrl);
                gGesture.selTrack = track.id;
                Playback::setPlayhead(gGesture.anchor);
                doc.sessionEdit.selection = Time::Range{gGesture.anchor, gGesture.anchor};
            } else if (eff == Editor::Tool::Grabber && hit) {
                std::vector<std::string> ids = Editor::selectedClipIds(doc);
                bool onSel = false;
                for (const auto& id : ids)
                    if (id == hit->id) onSel = true;
                if (!onSel) {
                    if (ImGui::GetIO().KeyShift)
                        Editor::addClipToSelection(doc, track.id, hit->id);
                    else
                        Editor::selectClip(doc, track.id, hit->id);
                    ids = Editor::selectedClipIds(doc);
                }
                gGesture.kind = GestureKind::MoveClips;
                gGesture.clipIds = ids;
                gGesture.grabAt = atOrigin;
                gGesture.fromTrack = track.id;
                gGesture.toTrack = track.id;
                gGesture.delta = 0;
            } else if (eff == Editor::Tool::Trim && hit) {
                const auto edge = nearClipEdge(x0, x1, mx);
                const bool startEdge = edge.value_or(atOrigin - hit->start < ClipOps::end(*hit) - atOrigin);
                if (Editor::mode() == Editor::Mode::Spot && !ImGui::GetIO().KeyCtrl) {
                    auto spot = Spot::trim(doc, hit->id, startEdge, startEdge ? hit->start : ClipOps::end(*hit));
                    EditDrag::spotOrRun(doc, std::move(spot));
                } else {
                    gGesture.kind = startEdge ? GestureKind::TrimStart : GestureKind::TrimEnd;
                    gGesture.trimClip = hit->id;
                    gGesture.trimTo = startEdge ? hit->start : ClipOps::end(*hit);
                }
            }
        }
    }

    dl->PushClipRect(ImVec2(p0.x, p0.y + kRulersH), ImVec2(p0.x + avail.x, p0.y + avail.y - 14.0f), true);

    const double rate = doc.sampleRate > 0 ? doc.sampleRate : 48000.0;
    const double spb = doc.time.samplesPerBeat(rate);
    const long long beatStep = std::max(1LL, (long long)std::llround(spb));
    const int beatsPerBar = std::max(1, doc.time.meter.num);
    const long long barSamples = std::max(1LL, beatStep * beatsPerBar);

    for (int ti = 0; ti < (int)doc.tracks.size(); ++ti) {
        if (doc.tracks[(size_t)ti].hidden) continue;
        const float rowY = rulerTop + trackTopOffset(doc, ti);
        const float rowH = trackHeightPx(doc.tracks[(size_t)ti]);
        if (rowY + rowH < p0.y + kRulersH || rowY > p0.y + avail.y) continue;

        auto& track = doc.tracks[(size_t)ti];
        const float laneX0 = p0.x + kHeaderW;
        const float laneX1 = p0.x + avail.x;

        dl->AddRectFilled(ImVec2(laneX0, rowY), ImVec2(laneX1, rowY + rowH - 1.0f),
                          (ti & 1) ? IM_COL32(0x1b, 0x1b, 0x1b, 255) : IM_COL32(0x17, 0x17, 0x17, 255));
        dl->AddLine(ImVec2(laneX0, rowY + rowH - 1.0f), ImVec2(laneX1, rowY + rowH - 1.0f), C_BORDER);

        for (long long s = (visStart / beatStep) * beatStep; s <= visEnd; s += beatStep) {
            const float gx = laneX0 + (float)((double)(s - visStart) / (double)gSamplesPerPixel);
            if (gx < laneX0 || gx > laneX1) continue;
            const bool isBar = (s % barSamples) == 0;
            dl->AddLine(ImVec2(gx, rowY), ImVec2(gx, rowY + rowH - 1.0f),
                        isBar ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 9));
        }

        const Mix::MixPlan::TrackPlay* tplay = nullptr;
        if (plan && ti < (int)plan->tracks.size()) tplay = &plan->tracks[(size_t)ti];

        for (const auto& clip : track.clips) {
            long long drawStart = clip.start;
            long long drawLen = clip.length;
            if (gGesture.kind == GestureKind::MoveClips && gGesture.delta != 0) {
                for (const auto& id : gGesture.clipIds)
                    if (id == clip.id) drawStart = Time::satAdd(clip.start, gGesture.delta);
            } else if (gGesture.kind == GestureKind::TrimStart && gGesture.trimClip == clip.id) {
                const long long origEnd = ClipOps::end(clip);
                drawStart = std::min(gGesture.trimTo, origEnd - 1);
                drawLen = std::max(1LL, origEnd - drawStart);
            } else if (gGesture.kind == GestureKind::TrimEnd && gGesture.trimClip == clip.id) {
                drawLen = std::max(1LL, gGesture.trimTo - clip.start);
            }

            const float cx0 = laneX0 + (float)((double)(drawStart - visStart) / (double)gSamplesPerPixel);
            const float cx1 = laneX0 + (float)((double)(drawStart + drawLen - visStart) / (double)gSamplesPerPixel);
            if (cx1 < laneX0 || cx0 > laneX1) continue;

            const float cy0 = rowY + 2.0f;
            const float cy1 = rowY + rowH - 3.0f;
            bool selected = false;
            for (const auto& r : Editor::selection().clips)
                if (r.trackId == track.id && r.clipId == clip.id) selected = true;
            if (!selected) {
                selected = std::find(doc.sessionEdit.selectedClips.begin(), doc.sessionEdit.selectedClips.end(), clip.numId) != doc.sessionEdit.selectedClips.end();
            }

            const auto cc = clip.color.value_or(track.color);
            const ImU32 bodyCol = clip.muted
                ? IM_COL32(cc[0] / 3, cc[1] / 3, cc[2] / 3, 180)
                : IM_COL32((int)(cc[0] * 0.62f), (int)(cc[1] * 0.62f), (int)(cc[2] * 0.62f), 235);
            const ImU32 headerCol = IM_COL32((int)(cc[0] * 0.38f), (int)(cc[1] * 0.38f), (int)(cc[2] * 0.38f), 250);
            const ImU32 waveLineCol = IM_COL32(std::min(255, (int)cc[0] + 75), std::min(255, (int)cc[1] + 75), std::min(255, (int)cc[2] + 75), 245);
            const ImU32 waveFillCol = IM_COL32(std::min(255, (int)cc[0] + 45), std::min(255, (int)cc[1] + 45), std::min(255, (int)cc[2] + 45), 210);

            const float visCx0 = std::max(laneX0, cx0);
            const float visCx1 = std::min(laneX1, cx1);
            dl->AddRectFilled(ImVec2(visCx0, cy0), ImVec2(visCx1, cy1), bodyCol, 3.0f);
            const float barH = std::min(15.0f, (cy1 - cy0) * 0.35f);
            dl->AddRectFilled(ImVec2(visCx0, cy0), ImVec2(visCx1, cy0 + barH), headerCol, 3.0f);

            if (visCx1 - visCx0 > 22.0f) {
                const std::string& lbl = clip.name.empty() ? clip.id : clip.name;
                dl->PushClipRect(ImVec2(visCx0 + 3.0f, cy0), ImVec2(visCx1 - 3.0f, cy0 + barH), true);
                dl->AddText(ImVec2(visCx0 + 5.0f, cy0 + 1.0f), C_TEXT, lbl.c_str());
                dl->PopClipRect();
            }

            const float bodyTop = cy0 + barH + 1.0f;
            if (clip.isMidi || Session::isMidiKind(track.kind) || track.view == "notes") {
                drawMidiClipNotes(dl, clip, doc, cx0, cx1, bodyTop, cy1 - 2.0f, waveLineCol);
            } else if (tplay && track.view != "blocks") {
                for (const auto& cplay : tplay->clips) {
                    if (cplay.clip.id == clip.id && cplay.audio && cplay.audio->frames > 0) {
                        const int pxCols = std::clamp((int)(cx1 - cx0 - 4.0f), 1, 2048);
                        const Mix::Peaks& pk = Mix::peaksForClipPixels(clip.sourceId, *cplay.audio, clip, pxCols);
                        dl->PushClipRect(ImVec2(visCx0 + 1.0f, bodyTop), ImVec2(visCx1 - 1.0f, cy1 - 1.0f), true);
                        drawWaveform(dl, ImVec2(cx0 + 2.0f, bodyTop), ImVec2(cx1 - 2.0f, cy1 - 2.0f),
                                     pk, gWaveformScale, waveLineCol, waveFillCol);
                        dl->PopClipRect();
                        break;
                    }
                }
            }

            // Fade-in and fade-out curves
            if (clip.fadeIn.len > 0) {
                const float fx1 = std::min(cx1, cx0 + (float)((double)clip.fadeIn.len / (double)gSamplesPerPixel));
                dl->PushClipRect(ImVec2(visCx0, bodyTop), ImVec2(visCx1, cy1), true);
                drawFadeOverlay(dl, cx0, fx1, bodyTop, cy1, clip.fadeIn, true);
                dl->PopClipRect();
            }
            if (clip.fadeOut.len > 0) {
                const float fx0 = std::max(cx0, cx1 - (float)((double)clip.fadeOut.len / (double)gSamplesPerPixel));
                dl->PushClipRect(ImVec2(visCx0, bodyTop), ImVec2(visCx1, cy1), true);
                drawFadeOverlay(dl, fx0, cx1, bodyTop, cy1, clip.fadeOut, false);
                dl->PopClipRect();
            }

            dl->AddRect(ImVec2(visCx0, cy0), ImVec2(visCx1, cy1),
                        selected ? IM_COL32(255, 255, 255, 245) : IM_COL32(0, 0, 0, 180), 3.0f, 0, selected ? 1.8f : 1.0f);
        }

        // Live recording take preview on armed audio tracks (after edit_window.rs live_take)
        if (RecordBounce::isRecording() && track.recordArm && track.kind == Session::TrackKind::Audio && !track.inactive) {
            const long long recStart = RecordBounce::recordStartSample();
            const long long recEnd = std::max(recStart + 2400LL, Playback::playhead());
            const auto spanOpt = RecordBounce::liveSpan(recStart, std::nullopt, RecordBounce::recordPunchRange(), recEnd - recStart);
            if (spanOpt.has_value()) {
                const Time::Range span = spanOpt->second;
                const float rx0 = std::max(laneX0, laneX0 + (float)((double)(span.start - visStart) / (double)gSamplesPerPixel));
                const float rx1 = std::min(laneX1, laneX0 + (float)((double)(span.end - visStart) / (double)gSamplesPerPixel));
                if (rx1 > rx0) {
                    const float cy0 = rowY + 2.0f;
                    const float cy1 = rowY + rowH - 3.0f;
                    const float barH = std::min(15.0f, (cy1 - cy0) * 0.35f);
                    dl->AddRectFilled(ImVec2(rx0, cy0), ImVec2(rx1, cy1), IM_COL32(0x7a, 0x1e, 0x1e, 225), 3.0f);
                    dl->AddRectFilled(ImVec2(rx0, cy0), ImVec2(rx1, cy0 + barH), IM_COL32(0xa8, 0x24, 0x24, 245), 3.0f);
                    dl->PushClipRect(ImVec2(rx0 + 3.0f, cy0), ImVec2(rx1 - 3.0f, cy0 + barH), true);
                    dl->AddText(ImVec2(rx0 + 5.0f, cy0 + 1.0f), C_TEXT, (track.name + "_01 (recording)").c_str());
                    dl->PopClipRect();

                    if (auto lt = RecordBounce::currentLiveTake(track.numId); lt.has_value() && !lt->empty() && !(*lt)[0].empty()) {
                        const auto& blocks = (*lt)[0];
                        Mix::Peaks pk;
                        pk.buckets = static_cast<int>(blocks.size());
                        pk.minMax.resize(blocks.size() * 2);
                        for (size_t bi = 0; bi < blocks.size(); ++bi) {
                            pk.minMax[bi * 2] = blocks[bi].first;
                            pk.minMax[bi * 2 + 1] = blocks[bi].second;
                        }
                        const float bodyTop = cy0 + barH + 1.0f;
                        dl->PushClipRect(ImVec2(rx0 + 1.0f, bodyTop), ImVec2(rx1 - 1.0f, cy1 - 1.0f), true);
                        drawWaveform(dl, ImVec2(rx0 + 2.0f, bodyTop), ImVec2(rx1 - 2.0f, cy1 - 2.0f),
                                     pk, gWaveformScale, IM_COL32(255, 180, 180, 245), IM_COL32(235, 110, 110, 210));
                        dl->PopClipRect();
                    }
                    dl->AddRect(ImVec2(rx0, cy0), ImVec2(rx1, cy1), C_REC, 3.0f, 0, 1.8f);
                }
            }
        }

        const bool isAutoView = (track.view == "volume" || track.view == "pan" || track.view == "mute")
                             || (track.view.empty() && (track.kind == Session::TrackKind::Aux || track.kind == Session::TrackKind::Master));
        if (isAutoView) {
            drawAutomationLane(dl, doc, track, laneX0, laneX1, rowY, rowH, visStart);
        }

        if (Editor::selection().end > Editor::selection().start) {
            if (Editor::selection().trackId.empty() || Editor::selection().trackId == track.id) {
                const float sx0 = std::max(laneX0, laneX0 + (float)((Editor::selection().start - visStart) / gSamplesPerPixel));
                const float sx1 = std::min(laneX1, laneX0 + (float)((Editor::selection().end - visStart) / gSamplesPerPixel));
                if (sx1 > sx0) {
                    dl->AddRectFilled(ImVec2(sx0, rowY), ImVec2(sx1, rowY + rowH - 1.0f), IM_COL32(0x3a, 0x84, 0xd8, 55));
                    dl->AddLine(ImVec2(sx0, rowY), ImVec2(sx0, rowY + rowH - 1.0f), C_GRID, 1.5f);
                    dl->AddLine(ImVec2(sx1, rowY), ImVec2(sx1, rowY + rowH - 1.0f), C_GRID, 1.5f);
                }
            }
        }

        drawTrackHeader(dl, doc, ti, p0.x, rowY, rowH);
    }

    // Marker vertical lines across lanes
    for (const auto& m : doc.markers) {
        const float mx = p0.x + kHeaderW + (float)((m.start - visStart) / gSamplesPerPixel);
        if (mx >= p0.x + kHeaderW && mx <= p0.x + avail.x) {
            dl->AddLine(ImVec2(mx, p0.y + kRulersH), ImVec2(mx, p0.y + avail.y - 14.0f),
                        IM_COL32(0xff, 0xc8, 0x42, 95), 1.0f);
        }
    }

    // Playhead vertical line
    const long long head = Playback::playhead();
    const float playX = p0.x + kHeaderW + (float)((head - visStart) / gSamplesPerPixel);
    if (playX >= p0.x + kHeaderW && playX <= p0.x + avail.x) {
        dl->AddLine(ImVec2(playX, p0.y + kRulersH), ImVec2(playX, p0.y + avail.y - 14.0f),
                    IM_COL32(0xff, 0xdd, 0x55, 235), 1.5f);
    }

    dl->PopClipRect();

    // Bottom horizontal scrollbar bar
    {
        const float sbY = p0.y + avail.y - 13.0f;
        const float sbX0 = p0.x + kHeaderW;
        const float sbX1 = p0.x + avail.x;
        dl->AddRectFilled(ImVec2(p0.x, sbY), ImVec2(sbX1, p0.y + avail.y), C_BG1);
        dl->AddLine(ImVec2(p0.x, sbY), ImVec2(sbX1, sbY), C_BORDER);
        long long sessionEnd = (long long)(doc.sampleRate * 36.0);
        for (const auto& t : doc.tracks) {
            for (const auto& c : t.clips) sessionEnd = std::max(sessionEnd, ClipOps::end(c) + (long long)(doc.sampleRate * 4.0));
        }
        const float visSpan = laneW * gSamplesPerPixel;
        const float totalSpan = std::max((float)sessionEnd, scrollX + visSpan);
        const float thumbW = std::max(28.0f, laneW * (visSpan / std::max(1.0f, totalSpan)));
        const float maxScroll = std::max(1.0f, totalSpan - visSpan);
        const float thumbX = sbX0 + (laneW - thumbW) * std::clamp(scrollX / maxScroll, 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(thumbX, sbY + 2.0f), ImVec2(thumbX + thumbW, p0.y + avail.y - 2.0f), C_BG4, 3.0f);
    }

    ImGui::EndChild();
}

// ============================================================================
// Standalone Fades Dialog & Rename/Tempo Modals (REVIEW.md item 11 + dialogs.rs)
// ============================================================================

void drawModals(Session::Document& doc) {
    // 1. Standalone Fades Dialog (REVIEW.md item 11 + SoundCraft Dialog::Fades)
    if (gFadesDlg.open) {
        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin(T("Fades"), &gFadesDlg.open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            const ImVec2 pMin = ImGui::GetCursorScreenPos();
            const ImVec2 pSz(356.0f, 110.0f);
            const ImVec2 pMax(pMin.x + pSz.x, pMin.y + pSz.y);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pMin, pMax, C_LCD, 3.0f);
            dl->AddRect(pMin, pMax, C_BORDER, 3.0f);
            dl->AddLine(ImVec2(pMin.x + pSz.x * 0.5f, pMin.y), ImVec2(pMin.x + pSz.x * 0.5f, pMax.y), IM_COL32(255, 255, 255, 25));
            dl->AddLine(ImVec2(pMin.x, pMin.y + pSz.y * 0.5f), ImVec2(pMax.x, pMin.y + pSz.y * 0.5f), IM_COL32(255, 255, 255, 25));

            const float pad = 10.0f;
            const float halfW = (pSz.x - pad * 3.0f) * 0.5f;
            const float boxH = pSz.y - pad * 2.0f;
            Session::Fade previewFade;
            previewFade.len = 4800;
            previewFade.shape = gFadesDlg.shape;
            previewFade.curvature = gFadesDlg.curvature;

            for (int side = 0; side < 2; ++side) {
                const bool isIn = (side == 0);
                const float x0 = isIn ? (pMin.x + pad) : (pMin.x + pad * 2.0f + halfW);
                const float y0 = pMin.y + pad;
                const float y1 = y0 + boxH;
                ImVec2 prev(x0, isIn ? y1 : y0);
                for (int s = 1; s <= 40; ++s) {
                    const float t = (float)s / 40.0f;
                    const float pos = isIn ? t : (1.0f - t);
                    const float g = ClipOps::fadeGain(previewFade, pos);
                    const float px = x0 + halfW * t;
                    const float py = y1 - boxH * std::clamp(g, 0.0f, 1.0f);
                    dl->AddLine(prev, ImVec2(px, py), isIn ? C_LCD_TEXT : C_BLUE, 2.0f);
                    prev = ImVec2(px, py);
                }
                dl->AddText(ImVec2(x0 + 4.0f, y0 + 2.0f), C_DIM, isIn ? "Fade In" : "Fade Out");
            }
            ImGui::Dummy(pSz);

            ImGui::Spacing();
            ImGui::TextUnformatted(T("Fade Shape:"));
            static const struct { const char* label; Session::FadeShape shape; } kShapes[] = {
                {"Linear",       Session::FadeShape::Linear},
                {"Equal Power",  Session::FadeShape::EqualPower},
                {"Exponential",  Session::FadeShape::Exponential},
                {"Logarithmic",  Session::FadeShape::Logarithmic},
                {"S-Curve",      Session::FadeShape::SCurve},
            };
            for (const auto& item : kShapes) {
                if (ImGui::RadioButton(item.label, gFadesDlg.shape == item.shape)) {
                    gFadesDlg.shape = item.shape;
                }
            }

            ImGui::SliderFloat(T("Curvature"), &gFadesDlg.curvature, -1.0f, 1.0f, "%.2f");
            ImGui::SliderFloat(T("Default Length (ms)"), &gFadesDlg.defaultFadeMs, 5.0f, 2000.0f, "%.0f ms");

            ImGui::Separator();
            if (ImGui::Button(T("Cancel"))) {
                gFadesDlg.open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(T("OK")) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                const Session::FadeShape chosenShape = gFadesDlg.shape;
                const float chosenCurve = gFadesDlg.curvature;
                const long long defaultFadeSamples = std::max(1LL, (long long)std::llround((gFadesDlg.defaultFadeMs / 1000.0) * doc.sampleRate));
                App::mutateDocument("Create Fades", [=](Session::Document& d) {
                    EngCmd::Engine e;
                    e.doc = std::move(d);
                    EngCmd::Engine::Params p;
                    p.strs["shape"] = ClipOps::fadeShapeLabel(chosenShape);
                    p.nums["curvature"] = (double)chosenCurve;
                    p.nums["length"] = (double)defaultFadeSamples;
                    e.execute("edit.fades_create", p);
                    d = std::move(e.doc);
                    for (auto& tr : d.tracks) {
                        for (auto& cl : tr.clips) {
                            bool sel = false;
                            for (const auto& r : Editor::selection().clips)
                                if (r.clipId == cl.id) sel = true;
                            if (sel) {
                                if (cl.fadeIn.len == 0 && cl.fadeOut.len == 0) {
                                    const long long fl = std::min(defaultFadeSamples, cl.length / 3);
                                    cl.fadeIn.len = fl;
                                    cl.fadeOut.len = fl;
                                }
                                cl.fadeIn.shape = chosenShape;
                                cl.fadeIn.curvature = chosenCurve;
                                cl.fadeOut.shape = chosenShape;
                                cl.fadeOut.curvature = chosenCurve;
                            }
                        }
                    }
                });
                gFadesDlg.open = false;
            }
        }
        ImGui::End();
    }

    // 2. Rename Track / Rename Marker / Tempo Change Modal
    if (gRenameModal.open) {
        const char* title = gRenameModal.kind == 0 ? "Rename Track" : (gRenameModal.kind == 1 ? "Rename Marker" : "Tempo Change (BPM)");
        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin(title, &gRenameModal.open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            bool submit = false;
            if (gRenameModal.kind == 2) {
                ImGui::SliderFloat("BPM", &gRenameModal.bpm, 20.0f, 300.0f, "%.1f");
            } else {
                ImGui::SetNextItemWidth(280.0f);
                submit = ImGui::InputText("##rename_modal_in", gRenameModal.nameBuf, sizeof(gRenameModal.nameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            }
            if (ImGui::Button("Cancel")) {
                gRenameModal.open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("OK") || submit) {
                const RenameModalState st = gRenameModal;
                if (st.kind == 0) {
                    App::mutateDocument("Rename Track", [st](Session::Document& d) {
                        for (auto& t : d.tracks)
                            if (t.id == st.targetId || t.numId == st.targetNumId) t.name = st.nameBuf;
                    });
                } else if (st.kind == 1) {
                    App::mutateDocument("Rename Marker", [st](Session::Document& d) {
                        for (auto& m : d.markers)
                            if (m.id == st.targetId) m.name = st.nameBuf;
                    });
                } else if (st.kind == 2) {
                    App::mutateDocument("Tempo Change", [st](Session::Document& d) {
                        d.time.tempos.push_back(Time::TempoEvent{st.sampleAt, (double)st.bpm});
                        if (st.sampleAt == 0) d.time.tempoBpm = (double)st.bpm;
                    });
                }
                gRenameModal.open = false;
            }
        }
        ImGui::End();
    }
}

void scriptSetup(Session::Document& doc) {
    scrollX = 0;
    scrollY = 0;
    const long long cEnd = Session::contentEnd(doc);
    if (doc.tracks.size() >= 8 && cEnd > 0) {
        gSamplesPerPixel = (float)std::max(128.0, (double)cEnd / 820.0);
        Playback::setPlayhead(doc.sessionEdit.selection.start);
    } else {
        gSamplesPerPixel = 256.0f;
    }
    Editor::setMode(Editor::Mode::Grid);
    Editor::setTool(Editor::Tool::Smart);
    gGesture = {};
    doc.touch++;
}

void openSpotForScript(Session::Document& doc) {
    if (doc.tracks.empty() || doc.tracks[0].clips.empty()) return;
    const auto& c = doc.tracks[0].clips[0];
    Editor::setMode(Editor::Mode::Spot);
    Editor::selectClip(doc, doc.tracks[0].id, c.id);
    auto spot = Spot::clips(doc, {c.id}, std::nullopt, 0);
    if (spot) Spot::open(std::move(*spot));
}

float samplesPerPixel() { return gSamplesPerPixel; }

void setSamplesPerPixel(float spp) {
    gSamplesPerPixel = std::clamp(spp, 1.0f, 100000.0f);
}

long long sampleAtX(float localX, float headerW) {
    (void)headerW;
    const long long visStart = (long long)scrollX;
    return visStart + (long long)(localX * gSamplesPerPixel);
}

DropTarget hitTestDrop(const Session::Document& doc, float screenX, float screenY) {
    DropTarget out;
    if (screenX < gTimelineMin.x || screenX > gTimelineMax.x || screenY < gTimelineMin.y || screenY > gTimelineMax.y) {
        return out;
    }
    out.inTimeline = true;
    const long long visStart = (long long)scrollX;
    if (screenX >= gTimelineMin.x + kHeaderW) {
        const long long rawAt = std::max(0LL, visStart + (long long)((screenX - (gTimelineMin.x + kHeaderW)) * gSamplesPerPixel));
        out.sampleAt = EditDrag::dragSnap(doc, rawAt, false);
    } else {
        out.sampleAt = Playback::playhead();
    }
    const int row = trackRowAt(doc, screenY, gRulerTopY);
    if (row >= 0 && doc.tracks[(size_t)row].kind == Session::TrackKind::Audio) {
        out.trackNumId = doc.tracks[(size_t)row].numId;
    } else {
        out.trackNumId = 0; // empty space below tracks -> create a new track
    }
    return out;
}

} // namespace Edit
