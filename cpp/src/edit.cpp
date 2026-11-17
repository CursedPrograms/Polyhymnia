// Polyhymnia - timeline with dual rulers, track lanes, clip waveforms and playhead.
#include "edit.h"
#include "app.h"
#include "editor.h"
#include "lang.h"
#include "mix.h"
#include "tempo.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cmath>

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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + kHeaderW, p0.y + contentH), C_BG);
    dl->AddRectFilled(ImVec2(p0.x + kHeaderW, p0.y), ImVec2(p0.x + avail.x, p0.y + contentH), C_WELL);
    drawRulers(dl, ImVec2(p0.x + kHeaderW, p0.y - scrollY), laneW, doc);
    dl->AddText(ImVec2(p0.x + 8, p0.y + 4 - scrollY), C_DIM, T("Tracks"));
    dl->AddText(ImVec2(p0.x + 8, p0.y + 14 - scrollY), C_DIM, T("Bars / beats"));
    dl->AddText(ImVec2(p0.x + 8, p0.y + kRulerH * 0.5f + 10 - scrollY), C_DIM, T("Min:sec"));

    const long long visStart = (long long)scrollX;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float mx = ImGui::GetIO().MousePos.x;
        const float my = ImGui::GetIO().MousePos.y;
        const long long clickSample = visStart + (long long)((mx - (p0.x + kHeaderW)) * gSamplesPerPixel);
        const int row = (int)((my - (p0.y + kRulerH - scrollY)) / kTrackH);
        if (row >= 0 && row < (int)doc.tracks.size() && clickSample >= 0) {
            const auto& track = doc.tracks[(size_t)row];
            const auto& tool = Editor::tool();
            if (tool == Editor::Tool::Selector) {
                Editor::selectRange(clickSample, clickSample + Editor::gridStepSamples(doc), track.id);
            } else if (tool == Editor::Tool::Grabber || tool == Editor::Tool::Smart) {
                for (const auto& clip : track.clips) {
                    if (clickSample >= clip.start && clickSample < clip.start + clip.length)
                        Editor::selectClip(doc, track.id, clip.id);
                }
            } else if (tool == Editor::Tool::Trim && Editor::selection().clips.size() == 1) {
                const auto& r = Editor::selection().clips[0];
                if (r.trackId == track.id) {
                    const Session::Clip* sc = nullptr;
                    for (const auto& c : track.clips)
                        if (c.id == r.clipId) sc = &c;
                    if (sc) {
                        const long long mid = sc->start + sc->length / 2;
                        const bool head = clickSample < mid;
                        App::mutateDocument(head ? "Trim start" : "Trim end", [&](Session::Document& d) {
                            if (head)
                                Editor::trimClipStart(d, r.trackId, r.clipId, clickSample);
                            else
                                Editor::trimClipEnd(d, r.trackId, r.clipId, clickSample);
                        });
                    }
                }
            }
        }
    }
    for (int ti = 0; ti < (int)doc.tracks.size(); ++ti) {
        const float rowY = p0.y + kRulerH + (float)ti * kTrackH - scrollY;
        const auto& track = doc.tracks[(size_t)ti];
        dl->AddRectFilled(ImVec2(p0.x, rowY), ImVec2(p0.x + kHeaderW, rowY + kTrackH - 2), C_PANEL);
        dl->AddText(ImVec2(p0.x + 8, rowY + 8), C_TEXT, track.name.c_str());
        char flags[32];
        snprintf(flags, sizeof(flags), "%s%s", track.mute ? "M" : "-", track.solo ? "S" : "-");
        dl->AddText(ImVec2(p0.x + 8, rowY + 28), C_DIM, flags);
        dl->AddLine(ImVec2(p0.x + kHeaderW, rowY + kTrackH - 1), ImVec2(p0.x + avail.x, rowY + kTrackH - 1), C_LINE);
        const Mix::MixPlan::TrackPlay* tplay = nullptr;
        if (plan && ti < (int)plan->tracks.size()) tplay = &plan->tracks[(size_t)ti];
        for (const auto& clip : track.clips) {
            const float x0 = p0.x + kHeaderW + (float)((clip.start - visStart) / gSamplesPerPixel);
            const float x1 = p0.x + kHeaderW + (float)((clip.start + clip.length - visStart) / gSamplesPerPixel);
            if (x1 < p0.x + kHeaderW || x0 > p0.x + avail.x) continue;
            const float y0 = rowY + 4;
            const float y1 = rowY + kTrackH - 6;
            bool selected = false;
            for (const auto& r : Editor::selection().clips)
                if (r.trackId == track.id && r.clipId == clip.id) selected = true;
            const ImU32 fill = selected ? IM_COL32(0x00, 0x7b, 0xb7, 220) : C_CLIP;
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill, 2.0f);
            if (tplay) {
                for (const auto& cplay : tplay->clips)
                    if (cplay.clip.id == clip.id && cplay.audio && cplay.audio->frames > 0) {
                        const int pxCols = std::max(1, (int)(x1 - x0 - 4.0f));
                        const Mix::Peaks& pk = Mix::peaksForClipPixels(clip.sourceId, *cplay.audio, clip, pxCols);
                        drawWaveform(dl, ImVec2(x0 + 2, y0 + 2), ImVec2(x1 - 2, y1 - 2), pk, IM_COL32(0xd6, 0xd6, 0xd6, 200));
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
        const float x = p0.x + kHeaderW + (float)((m.at - visStart) / gSamplesPerPixel);
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
    doc.touch++;
}

float samplesPerPixel() { return gSamplesPerPixel; }

long long sampleAtX(float localX, float headerW) {
    (void)headerW;
    const long long visStart = (long long)scrollX;
    return visStart + (long long)(localX * gSamplesPerPixel);
}

} // namespace Edit