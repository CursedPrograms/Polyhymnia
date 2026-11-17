// Spot dialog UI and commands (after SoundCraft dialogs/spot.rs).
#include "spot_dialog.h"
#include "app.h"
#include "clip_ops.h"
#include "editor.h"
#include "engine.h"
#include "engine_edit.h"
#include "time_format.h"
#include "time_range.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace Spot {

static std::optional<State> gOpen;

static long long value(const State& d, int field) {
    switch (field) {
    case 0: return d.start;
    case 1: return Time::satAdd(d.start, d.sync);
    default: return Time::satAdd(d.start, d.len);
    }
}

static void setValue(State& d, int field, long long v) {
    if (d.target == TargetKind::TrimStart) {
        d.len = Time::satSub(Time::satAdd(d.start, d.len), v);
        d.start = v;
        return;
    }
    if (d.target == TargetKind::TrimEnd) {
        d.len = Time::satSub(v, d.start);
        return;
    }
    switch (field) {
    case 0: d.start = v; break;
    case 1: d.start = Time::satSub(v, d.sync); break;
    default: d.start = Time::satSub(v, d.len); break;
    }
}

static bool editable(const State& d, int field) {
    if (d.target == TargetKind::TrimStart) return field == 0;
    if (d.target == TargetKind::TrimEnd) return field == 2;
    return true;
}

bool isOpen() { return gOpen.has_value(); }
void close() { gOpen.reset(); }

std::optional<State> clips(const Session::Document& s, std::vector<std::string> clipIds, std::optional<std::string> track, long long delta) {
    const Session::Clip* c = EngEdit::firstMovable(s, clipIds);
    if (!c) return std::nullopt;
    State st;
    st.target = TargetKind::Clips;
    st.clipIds = std::move(clipIds);
    if (!st.clipIds.empty())
        if (auto fc = Session::findClip(s, st.clipIds[0])) st.fromTrack = fc->first;
    if (track) st.toTrack = track;
    st.start = Time::satAdd(c->start, delta);
    st.len = c->length;
    st.sync = c->syncPoint;
    if (!c->isMidi) {
        if (const Session::Source* src = Session::findSource(s, c->sourceId)) {
            const long long recAt = Time::satSub(static_cast<long long>(src->timeReference & 0x7FFFFFFFFFFFFFFFULL), s.timecodeStart);
            st.stamp = Time::satAdd(recAt, ClipOps::sourceOffset(*c));
        }
    }
    st.format = TimeFormat::Kind::MinSecs;
    st.fresh = true;
    return st;
}

std::optional<State> trim(const Session::Document& s, const std::string& clipId, bool startEdge, long long to) {
    auto fc = Session::findClip(s, clipId);
    if (!fc) return std::nullopt;
    const Session::Clip& c = *fc->second;
    State st;
    st.trimClip = clipId;
    st.trimStart = startEdge;
    st.target = startEdge ? TargetKind::TrimStart : TargetKind::TrimEnd;
    if (startEdge) {
        st.start = std::min(to, Time::satSub(ClipOps::end(c), 1LL));
        st.len = Time::satSub(ClipOps::end(c), st.start);
    } else {
        st.start = c.start;
        st.len = Time::satSub(std::max(to, Time::satAdd(c.start, 1LL)), c.start);
    }
    st.sync = 0;
    st.format = TimeFormat::Kind::MinSecs;
    st.fresh = true;
    return st;
}

// after SoundCraft (crates/ui-egui/src/dialogs/spot.rs Spot::source)
std::optional<State> source(const Session::Document& s, uint64_t sourceNumId, uint64_t trackNumId, long long at) {
    const Session::Source* src = Session::findSourceByNum(s, sourceNumId);
    if (!src) return std::nullopt;
    State st;
    st.target = TargetKind::Source;
    st.sourceNumId = sourceNumId;
    st.sourceTrackNumId = trackNumId;
    st.start = at;
    st.len = std::max(0LL, src->frames);
    st.sync = 0;
    st.stamp = Time::satSub(static_cast<long long>(src->timeReference & 0x7FFFFFFFFFFFFFFFULL), s.timecodeStart);
    st.format = TimeFormat::Kind::MinSecs;
    st.fresh = true;
    return st;
}

void open(State s) { gOpen = std::move(s); }

bool apply(const State& st, Session::Document& doc) {
    if (st.target == TargetKind::Clips) {
        const Session::Clip* first = EngEdit::firstMovable(doc, st.clipIds);
        if (!first) return false;
        const long long by = Time::satSub(st.start, first->start);
        if (by == 0 && !st.toTrack) return false;
        std::vector<std::pair<std::string, std::string>> dest;
        if (st.toTrack && st.fromTrack != *st.toTrack) {
            const auto d = EngEdit::destinations(doc, st.clipIds, st.fromTrack, *st.toTrack);
            if (!d) return false;
            dest = *d;
        }
        if (by != 0 || !dest.empty()) {
            if (Editor::mode() == Editor::Mode::Shuffle)
                EngEdit::shuffleClips(doc, st.clipIds, by, dest);
            else
                EngEdit::moveClips(doc, st.clipIds, by, dest);
        }
        ++doc.touch;
        return true;
    }
    if (st.target == TargetKind::TrimStart) {
        if (!EngEdit::trimClip(doc, st.trimClip, st.start, std::nullopt, Editor::mode() == Editor::Mode::Shuffle)) return false;
        ++doc.touch;
        return true;
    }
    if (st.target == TargetKind::TrimEnd) {
        const long long end = value(st, 2);
        if (!EngEdit::trimClip(doc, st.trimClip, std::nullopt, end, Editor::mode() == Editor::Mode::Shuffle)) return false;
        ++doc.touch;
        return true;
    }
    if (st.target == TargetKind::Source) {
        EngCmd::Engine e;
        e.doc = std::move(doc);
        EngCmd::Engine::Params p;
        p.nums["source"] = static_cast<double>(st.sourceNumId);
        if (st.sourceTrackNumId != 0) p.nums["track"] = static_cast<double>(st.sourceTrackNumId);
        p.nums["at"] = static_cast<double>(st.start);
        const auto r = e.execute("clip.place_source", p);
        doc = std::move(e.doc);
        if (r.ok) ++doc.touch;
        return r.ok;
    }
    return false;
}

bool draw(Session::Document& doc) {
    if (!gOpen) return false;
    State& d = *gOpen;
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Spot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return false;
    }
    int fmtIdx = (int)d.format;
    if (ImGui::Combo("Time Scale", &fmtIdx, "Bars|Beats\0Min:Secs\0Timecode\0Feet+Frames\0Samples\0")) d.format = TimeFormat::allKinds((size_t)fmtIdx);
    const char* labels[] = {"Start", "Sync Point", "End"};
    for (int i = 0; i < 3; ++i) {
        if (i == 1 && (d.target == TargetKind::TrimStart || d.target == TargetKind::TrimEnd)) continue;
        const bool edit = editable(d, i);
        ImGui::PushID(i);
        if (edit) {
            if (!ImGui::IsAnyItemActive() && (d.fresh || d.text[i].empty())) d.text[i] = TimeFormat::formatPosition(value(d, i), d.format, doc);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s", d.text[i].c_str());
            if (ImGui::InputText(labels[i], buf, sizeof(buf))) {
                d.text[i] = buf;
                if (auto v = TimeFormat::parsePosition(d.text[i], d.format, doc)) {
                    if (*v >= -ClipOps::kMaxPosition && *v <= ClipOps::kMaxPosition) setValue(d, i, *v);
                }
            }
            if (d.fresh && i == 0) ImGui::SetKeyboardFocusHere(-1);
        } else {
            ImGui::Text("%s", labels[i]);
            ImGui::SameLine(120);
            ImGui::TextUnformatted(TimeFormat::formatPosition(value(d, i), d.format, doc).c_str());
        }
        ImGui::PopID();
    }
    d.fresh = false;
    ImGui::Separator();
    ImGui::Text("Duration");
    ImGui::SameLine(120);
    ImGui::TextUnformatted(TimeFormat::formatLength(d.start, d.len, d.format, doc).c_str());
    if (d.stamp.has_value()) {
        ImGui::Text("Original Time Stamp");
        ImGui::SameLine(150);
        ImGui::TextUnformatted(TimeFormat::formatPosition(*d.stamp, TimeFormat::Kind::Timecode, doc).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Use")) {
            d.start = *d.stamp;
            d.fresh = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Spot to where it was recorded");
        }
    }
    bool close = false;
    if (ImGui::Button("OK")) {
        close = true;
        App::mutateDocument("Spot", [&](Session::Document& mut) { apply(d, mut); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) close = true;
    ImGui::End();
    if (close) gOpen.reset();
    return close;
}

} // namespace Spot