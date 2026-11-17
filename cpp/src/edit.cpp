// Polyhymnia - timeline with dual rulers, track lanes, clip waveforms and playhead.
#include "edit.h"
#include "lang.h"
#include "mix.h"
#include "tempo.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace Edit {

static float samplesPerPixel = 128.0f;
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
        const float x0 = tl.x + w * (float)b / (float)buckets;
        const float x1 = tl.x + w * (float)(b + 1) / (float)buckets;
        const float mn = peaks.minMax[(size_t)b * 2];
        const float mx = peaks.minMax[(size_t)b * 2 + 1];
        dl->AddLine(ImVec2(x0, mid - mx * h * 0.45f), ImVec2(x1, mid - mn * h * 0.45f), col, 1.0f);
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
    const long long visEnd = visStart + (long long)(width * samplesPerPixel);
    const long long beatStep = std::max(1LL, (long long)spb);
    const long long barSamples = (long long)(spb * doc.time.meter.num);
    for (long long s = (visStart / beatStep) * beatStep; s < visEnd; s += beatStep) {
        const float x = origin.x + (float)((s - visStart) / samplesPerPixel);
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
        const float x = origin.x + (float)((s - visStart) / samplesPerPixel);
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
        scrollX -= ImGui::GetIO().MouseDelta.x * samplesPerPixel;
        scrollY -= ImGui::GetIO().MouseDelta.y;
        if (scrollX < 0) scrollX = 0;
        if (scrollY < 0) scrollY = 0;
    }
    if (hovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (ImGui::GetIO().KeyShift)
                scrollX -= wheel * samplesPerPixel * 32.0f;
            else
                samplesPerPixel = std::clamp(samplesPerPixel * (wheel > 0 ? 0.9f : 1.1f), 8.0f, 4096.0f);
        }
    }

    const ImVec2 p0 = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + kHeaderW, p0.y + contentH), C_BG);
    dl->AddRectFilled(ImVec2(p0.x + kHeaderW, p0.y), ImVec2(p0.x + avail.x, p0.y + contentH), C_WELL);
    drawRulers(dl, ImVec2(p0.x + kHeaderW, p0.y - scrollY), laneW, doc);
    dl->AddText(ImVec2(p0.x + 8, p0.y + 4 - scrollY), C_DIM, T("Tracks"));
    dl->AddText(ImVec2(p0.x + kHeaderW + 8, p0.y + 4 - scrollY), C_DIM, T("Bars / beats"));
    dl->AddText(ImVec2(p0.x + kHeaderW + 8, p0.y + kRulerH * 0.5f + 4 - scrollY), C_DIM, T("Min:sec"));

    const long long visStart = (long long)scrollX;
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
            const float x0 = p0.x + kHeaderW + (float)((clip.start - visStart) / samplesPerPixel);
            const float x1 = p0.x + kHeaderW + (float)((clip.start + clip.length - visStart) / samplesPerPixel);
            if (x1 < p0.x + kHeaderW || x0 > p0.x + avail.x) continue;
            const float y0 = rowY + 4;
            const float y1 = rowY + kTrackH - 6;
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), C_CLIP, 2.0f);
            if (tplay) {
                for (const auto& cp : tplay->clips)
                    if (cp.clip.id == clip.id) {
                        drawWaveform(dl, ImVec2(x0 + 2, y0 + 2), ImVec2(x1 - 2, y1 - 2), cp.peaks, IM_COL32(0xd6, 0xd6, 0xd6, 180));
                        break;
                    }
            }
        }
    }
    const long long head = Playback::playhead();
    const float playX = p0.x + kHeaderW + (float)((head - visStart) / samplesPerPixel);
    if (playX >= p0.x + kHeaderW && playX <= p0.x + avail.x)
        dl->AddLine(ImVec2(playX, p0.y - scrollY), ImVec2(playX, p0.y + contentH - scrollY), C_BLUE, 2.0f);
    for (const auto& m : doc.markers) {
        const float x = p0.x + kHeaderW + (float)((m.at - visStart) / samplesPerPixel);
        if (x < p0.x + kHeaderW || x > p0.x + avail.x) continue;
        dl->AddLine(ImVec2(x, p0.y - scrollY), ImVec2(x, p0.y + contentH - scrollY), IM_COL32(0xff, 0xcc, 0x44, 200), 1.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + contentH));
    ImGui::TextDisabled("%s %.0f  %s %lld", T("Zoom"), samplesPerPixel, T("Playhead"), (long long)head);
    ImGui::EndChild();
}

void scriptSetup(Session::Document& doc) {
    scrollX = 0;
    samplesPerPixel = 256;
    doc.touch++;
}

} // namespace Edit