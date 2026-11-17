// Polyhymnia - timeline with dual rulers, track lanes, clip waveforms and playhead.
#include "edit.h"
#include "app.h"
#include "clip_ops.h"
#include "clips_panel.h"
#include "edit_drag.h"
#include "editor.h"
#include "lang.h"
#include "mix.h"
#include "spot_dialog.h"
#include "tempo.h"
#include "time_range.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace Edit {

static float gSamplesPerPixel = 128.0f;
static float scrollX = 0.0f;
static float scrollY = 0.0f;
static const float kTrackH = 72.0f;
static const float kRulerH = 44.0f;
static const float kHeaderW = 140.0f;

static const ImU32 C_BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), C_PANEL = IM_COL32(0x21, 0x21, 0x21, 255), C_WELL = IM_COL32(0x2a, 0x2a, 0x2a, 255),
                   C_LINE = IM_COL32(0x33, 0x33, 0x33, 255), C_TEXT = IM_COL32(0xd6, 0xd6, 0xd6, 255), C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   C_BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255), C_CLIP = IM_COL32(0x00, 0x5c, 0x8a, 200);

// after SoundCraft edit_window/input.rs gestures + drag.rs drive_drag / finish_drag
enum class GestureKind { None, Select, MoveClips, TrimStart, TrimEnd };

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
};

static Gesture gGesture;
static bool gHadDrag = false;
static ImVec2 gTimelineMin = ImVec2(0, 0);
static ImVec2 gTimelineMax = ImVec2(0, 0);
static float gRulerTopY = 0.0f;

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

// after input.rs effective_tool
static Editor::Tool effectiveTool(Editor::Tool tool, const Session::Clip* hit, float laneMidY, float my, float mx, float x0, float x1) {
    if (tool != Editor::Tool::Smart) return tool;
    if (!hit) return Editor::Tool::Selector;
    if (nearClipEdge(x0, x1, mx)) return Editor::Tool::Trim;
    return my < laneMidY ? Editor::Tool::Selector : Editor::Tool::Grabber;
}

static int trackRowAt(float contentY, float rulerTop, int trackCount) {
    const int row = (int)((contentY - rulerTop) / kTrackH);
    return row >= 0 && row < trackCount ? row : -1;
}

static std::string trackAtY(const Session::Document& doc, float contentY, float p0y, float rulerTop) {
    const int row = trackRowAt(contentY, rulerTop, (int)doc.tracks.size());
    return row >= 0 ? doc.tracks[(size_t)row].id : std::string{};
}

static void finishGesture(Session::Document& doc, float p0x, float headerW, float p0y, float rulerTop) {
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
    } else if (g.kind == GestureKind::Select && g.anchor != 0) {
        (void)p0x;
        (void)headerW;
        (void)p0y;
        (void)rulerTop;
    }
}

static void driveGesture(Session::Document& doc, float laneW, float p0x, float headerW, float p0y, float rulerTop) {
    if (gGesture.kind == GestureKind::None) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.MouseDown[0]) {
        finishGesture(doc, p0x, headerW, p0y, rulerTop);
        return;
    }
    const long long visStart = (long long)scrollX;
    const float mx = io.MousePos.x;
    const float my = io.MousePos.y;
    const long long at = visStart + (long long)((mx - (p0x + headerW)) * gSamplesPerPixel);
    const bool flip = io.KeyCtrl;
    if (gGesture.kind == GestureKind::Select) {
        const long long end = EditDrag::dragSnap(doc, at, flip);
        Editor::selectRange(std::min(gGesture.anchor, end), std::max(gGesture.anchor, end), gGesture.selTrack);
    } else if (gGesture.kind == GestureKind::MoveClips) {
        const std::string toTr = trackAtY(doc, my, p0y, rulerTop);
        if (!toTr.empty()) gGesture.toTrack = toTr;
        const auto plan = EditDrag::dragPlan(doc, doc.touch, gGesture.clipIds, gGesture.fromTrack, gGesture.toTrack, gGesture.grabAt);
        const long long raw = Time::satSub(at, gGesture.grabAt);
        gGesture.delta = EditDrag::moveDelta(*plan, doc, gGesture.fromTrack, raw, flip);
        gHadDrag = true;
    } else if (gGesture.kind == GestureKind::TrimStart)
        gGesture.trimTo = EditDrag::trimDragSnap(doc, gGesture.trimClip, at, true, flip);
    else if (gGesture.kind == GestureKind::TrimEnd)
        gGesture.trimTo = EditDrag::trimDragSnap(doc, gGesture.trimClip, at, false, flip);
    (void)laneW;
}

static void drawWaveform(ImDrawList* dl, const ImVec2& tl, const ImVec2& br, const Mix::Peaks& peaks, ImU32 col) {
    if (peaks.buckets <= 0 || peaks.minMax.empty()) return;
    const float w = br.x - tl.x;
    const float h = br.y - tl.y;
    const float mid = (tl.y + br.y) * 0.5f;
    const int buckets = peaks.buckets;
    for (int b = 0; b < buckets; ++b) {
        const float xc = tl.x + w * ((float)b + 0.5f) / (float)buckets;
        const float mn = peaks.minMax[(size_t)b * 2];
        const float mx = peaks.minMax[(size_t)b * 2 + 1];
        dl->AddLine(ImVec2(xc, mid - mx * h * 0.45f), ImVec2(xc, mid - mn * h * 0.45f), col, 1.0f);
    }
}

static void drawRulers(ImDrawList* dl, const ImVec2& origin, float width, const Session::Document& doc) {
    const double rate = doc.sampleRate;
    const double spb = doc.time.samplesPerBeat(rate);
    const float y0 = origin.y;
    const float yMid = y0 + kRulerH * 0.5f;
    dl->AddRectFilled(origin, ImVec2(origin.x + width, y0 + kRulerH), C_PANEL);
    dl->AddLine(ImVec2(origin.x, yMid), ImVec2(origin.x + width, yMid), C_LINE);
    const long long visStart = (long long)scrollX;
    const long long visEnd = visStart + (long long)(width * gSamplesPerPixel);
    const long long beatStep = std::max(1LL, (long long)spb);
    const long long barSamples = (long long)(spb * doc.time.meter.num);
    for (long long s = (visStart / beatStep) * beatStep; s < visEnd; s += beatStep) {
        const float x = origin.x + (float)((s - visStart) / gSamplesPerPixel);
        const bool bar = barSamples > 0 && (s % barSamples) == 0;
        dl->AddLine(ImVec2(x, y0 + (bar ? 4.0f : 12.0f)), ImVec2(x, y0 + kRulerH), bar ? C_TEXT : C_DIM);
        if (bar) {
            const int barNum = barSamples > 0 ? (int)(s / barSamples) + 1 : 0;
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", barNum);
            dl->AddText(ImVec2(x + 2, y0 + 2), C_TEXT, buf);
        }
    }
    const long long secStep = rate > 0 ? (long long)rate : 48000LL;
    for (long long s = (visStart / secStep) * secStep; s < visEnd; s += secStep) {
        const float x = origin.x + (float)((s - visStart) / gSamplesPerPixel);
        dl->AddLine(ImVec2(x, yMid), ImVec2(x, y0 + kRulerH), C_LINE);
        const int sec = (int)(s / (long long)rate);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", sec / 60, sec % 60);
        dl->AddText(ImVec2(x + 2, yMid + 2), C_DIM, buf);
    }
}

void draw(Session::Document& doc, float transportReserve) {
    ImGui::BeginChild("timeline", ImVec2(0, -transportReserve), false, ImGuiWindowFlags_NoScrollbar);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float laneW = std::max(10.0f, avail.x - kHeaderW);
    const auto plan = Mix::currentPlan();
    const int trackCount = std::max(1, (int)doc.tracks.size());
    const float contentH = kRulerH + (float)trackCount * kTrackH + 24.0f;

    ImGui::InvisibleButton("timeline_drag", ImVec2(avail.x, std::min(avail.y, contentH)));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        scrollX -= ImGui::GetIO().MouseDelta.x * gSamplesPerPixel;
        scrollY -= ImGui::GetIO().MouseDelta.y;
        if (scrollX < 0) scrollX = 0;
        if (scrollY < 0) scrollY = 0;
    }
    if (hovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (ImGui::GetIO().KeyShift)
                scrollX -= wheel * gSamplesPerPixel * 32.0f;
            else
                gSamplesPerPixel = std::clamp(gSamplesPerPixel * (wheel > 0 ? 0.9f : 1.1f), 8.0f, 4096.0f);
        }
    }

    const ImVec2 p0 = ImGui::GetItemRectMin();
    gTimelineMin = p0;
    gTimelineMax = ImVec2(p0.x + avail.x, p0.y + avail.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + kHeaderW, p0.y + std::max(avail.y, contentH)), C_BG);
    dl->AddRectFilled(ImVec2(p0.x + kHeaderW, p0.y), ImVec2(p0.x + avail.x, p0.y + std::max(avail.y, contentH)), C_WELL);
    drawRulers(dl, ImVec2(p0.x + kHeaderW, p0.y - scrollY), laneW, doc);
    dl->AddText(ImVec2(p0.x + 8, p0.y + 4 - scrollY), C_DIM, T("Tracks"));
    dl->AddText(ImVec2(p0.x + 8, p0.y + 14 - scrollY), C_DIM, T("Bars / beats"));
    dl->AddText(ImVec2(p0.x + 8, p0.y + kRulerH * 0.5f + 10 - scrollY), C_DIM, T("Min:sec"));

    const float rulerTop = p0.y + kRulerH - scrollY;
    gRulerTopY = rulerTop;
    const long long visStart = (long long)scrollX;

    // after SoundCraft (crates/ui-egui/src/edit_window/input.rs lane_menu_and_drops DragSource)
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* hoverPayload = ImGui::AcceptDragDropPayload(ClipsPanel::kDragSourcePayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (hoverPayload && hoverPayload->DataSize == sizeof(uint64_t)) {
            const float mx = ImGui::GetIO().MousePos.x;
            const float my = ImGui::GetIO().MousePos.y;
            const int row = trackRowAt(my, rulerTop, (int)doc.tracks.size());
            if (row >= 0) {
                const float ry = rulerTop + (float)row * kTrackH;
                dl->AddRect(ImVec2(p0.x + kHeaderW, ry), ImVec2(p0.x + avail.x, ry + kTrackH - 2.0f), C_BLUE, 0.0f, 0, 2.0f);
            } else {
                const float ry = rulerTop + (float)doc.tracks.size() * kTrackH;
                dl->AddRect(ImVec2(p0.x + kHeaderW, ry), ImVec2(p0.x + avail.x, ry + kTrackH - 2.0f), C_BLUE, 0.0f, 0, 2.0f);
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

    driveGesture(doc, laneW, p0.x, kHeaderW, p0.y, rulerTop);
    if (hovered && gGesture.kind == GestureKind::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float mx = ImGui::GetIO().MousePos.x;
        const float my = ImGui::GetIO().MousePos.y;
        const long long atOrigin = visStart + (long long)((mx - (p0.x + kHeaderW)) * gSamplesPerPixel);
        const int row = trackRowAt(my, rulerTop, (int)doc.tracks.size());
        if (row >= 0 && atOrigin >= 0) {
            const auto& track = doc.tracks[(size_t)row];
            const Editor::Tool tool = Editor::tool();
            const Session::Clip* hit = clipAt(track, atOrigin);
            float x0 = 0, x1 = 0;
            const float laneMid = p0.y + kRulerH + (float)row * kTrackH - scrollY + kTrackH * 0.5f;
            if (hit) {
                x0 = p0.x + kHeaderW + (float)((hit->start - visStart) / gSamplesPerPixel);
                x1 = p0.x + kHeaderW + (float)((ClipOps::end(*hit) - visStart) / gSamplesPerPixel);
            }
            const Editor::Tool eff = effectiveTool(tool, hit, laneMid, my, mx, x0, x1);
            if (eff == Editor::Tool::Selector || (eff == Editor::Tool::Smart && !hit)) {
                gGesture.kind = GestureKind::Select;
                gGesture.anchor = EditDrag::dragSnap(doc, atOrigin, ImGui::GetIO().KeyCtrl);
                gGesture.selTrack = track.id;
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
    for (int ti = 0; ti < (int)doc.tracks.size(); ++ti) {
        const float rowY = p0.y + kRulerH + (float)ti * kTrackH - scrollY;
        const auto& track = doc.tracks[(size_t)ti];
        const ImU32 trackCol = IM_COL32(track.color[0], track.color[1], track.color[2], 255);
        dl->AddRectFilled(ImVec2(p0.x, rowY), ImVec2(p0.x + kHeaderW, rowY + kTrackH - 2), C_PANEL);
        dl->AddRectFilled(ImVec2(p0.x, rowY), ImVec2(p0.x + 5, rowY + kTrackH - 2), trackCol);
        dl->AddText(ImVec2(p0.x + 10, rowY + 8), C_TEXT, track.name.c_str());
        char flags[32];
        snprintf(flags, sizeof(flags), "%s%s", track.mute ? "M" : "-", track.solo ? "S" : "-");
        dl->AddText(ImVec2(p0.x + 10, rowY + 28), C_DIM, flags);
        dl->AddLine(ImVec2(p0.x + kHeaderW, rowY + kTrackH - 1), ImVec2(p0.x + avail.x, rowY + kTrackH - 1), C_LINE);
        const Mix::MixPlan::TrackPlay* tplay = nullptr;
        if (plan && ti < (int)plan->tracks.size()) tplay = &plan->tracks[(size_t)ti];
        for (const auto& clip : track.clips) {
            long long drawStart = clip.start;
            if (gGesture.kind == GestureKind::MoveClips && gGesture.delta != 0) {
                for (const auto& id : gGesture.clipIds)
                    if (id == clip.id) drawStart = Time::satAdd(clip.start, gGesture.delta);
            }
            const float x0 = p0.x + kHeaderW + (float)((drawStart - visStart) / gSamplesPerPixel);
            const float x1 = p0.x + kHeaderW + (float)((drawStart + clip.length - visStart) / gSamplesPerPixel);
            if (x1 < p0.x + kHeaderW || x0 > p0.x + avail.x) continue;
            const float y0 = rowY + 4;
            const float y1 = rowY + kTrackH - 6;
            bool selected = false;
            for (const auto& r : Editor::selection().clips)
                if (r.trackId == track.id && r.clipId == clip.id) selected = true;
            const auto cc = clip.color.value_or(track.color);
            const ImU32 baseFill = IM_COL32(cc[0] * 3 / 5, cc[1] * 3 / 5, cc[2] * 3 / 5, 220);
            const ImU32 fill = selected ? IM_COL32(0x00, 0x7b, 0xb7, 235) : baseFill;
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill, 2.0f);
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, std::min(y1, y0 + 16.0f)), IM_COL32(0, 0, 0, 90), 2.0f);
            if (x1 - x0 > 24.0f) {
                const std::string& lbl = clip.name.empty() ? clip.id : clip.name;
                dl->PushClipRect(ImVec2(x0 + 2, y0), ImVec2(x1 - 2, y0 + 16.0f), true);
                dl->AddText(ImVec2(x0 + 4, y0 + 1), C_TEXT, lbl.c_str());
                dl->PopClipRect();
            }
            if (tplay) {
                for (const auto& cplay : tplay->clips)
                    if (cplay.clip.id == clip.id && cplay.audio && cplay.audio->frames > 0) {
                        const int pxCols = std::max(1, (int)(x1 - x0 - 4.0f));
                        const Mix::Peaks& pk = Mix::peaksForClipPixels(clip.sourceId, *cplay.audio, clip, pxCols);
                        drawWaveform(dl, ImVec2(x0 + 2, y0 + 16), ImVec2(x1 - 2, y1 - 2), pk, IM_COL32(0xe8, 0xe8, 0xe8, 220));
                        break;
                    }
            }
        }
    }
    const long long head = Playback::playhead();
    const float playX = p0.x + kHeaderW + (float)((head - visStart) / gSamplesPerPixel);
    if (playX >= p0.x + kHeaderW && playX <= p0.x + avail.x)
        dl->AddLine(ImVec2(playX, p0.y - scrollY), ImVec2(playX, p0.y + contentH - scrollY), C_BLUE, 2.0f);
    for (const auto& m : doc.markers) {
        const float x = p0.x + kHeaderW + (float)((m.start - visStart) / gSamplesPerPixel);
        if (x < p0.x + kHeaderW || x > p0.x + avail.x) continue;
        dl->AddLine(ImVec2(x, p0.y - scrollY), ImVec2(x, p0.y + contentH - scrollY), IM_COL32(0xff, 0xcc, 0x44, 200), 1.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + contentH));
    ImGui::TextDisabled("%s %.0f  %s %lld", T("Zoom"), gSamplesPerPixel, T("Playhead"), (long long)head);
    ImGui::EndChild();
}

void drawToolbar() {
    ImGui::BeginChild("edit_tools", ImVec2(0, 36), false, ImGuiWindowFlags_NoScrollbar);
    auto modeBtn = [](const char* label, Editor::Mode m) {
        const bool on = Editor::mode() == m;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(C_BLUE));
        if (ImGui::Button(label, ImVec2(72, 0))) Editor::setMode(m);
        if (on) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    modeBtn("Shuffle", Editor::Mode::Shuffle);
    modeBtn("Slip", Editor::Mode::Slip);
    modeBtn("Spot", Editor::Mode::Spot);
    modeBtn("Grid", Editor::Mode::Grid);
    modeBtn("Rel", Editor::Mode::GridRelative);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    auto toolBtn = [](const char* label, Editor::Tool t) {
        const bool on = Editor::tool() == t;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(C_BLUE));
        if (ImGui::Button(label, ImVec2(64, 0))) Editor::setTool(t);
        if (on) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    toolBtn("Smart", Editor::Tool::Smart);
    toolBtn("Sel", Editor::Tool::Selector);
    toolBtn("Grab", Editor::Tool::Grabber);
    toolBtn("Trim", Editor::Tool::Trim);
    ImGui::SameLine();
    bool snap = Editor::snapToGrid();
    if (ImGui::Checkbox(T("Snap"), &snap)) Editor::setSnapToGrid(snap);
    ImGui::EndChild();
}

void scriptSetup(Session::Document& doc) {
    scrollX = 0;
    gSamplesPerPixel = 256;
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
    const int row = trackRowAt(screenY, gRulerTopY, (int)doc.tracks.size());
    if (row >= 0 && doc.tracks[(size_t)row].kind == Session::TrackKind::Audio) {
        out.trackNumId = doc.tracks[(size_t)row].numId;
    } else {
        out.trackNumId = 0; // empty space below tracks -> create a new track
    }
    return out;
}

} // namespace Edit