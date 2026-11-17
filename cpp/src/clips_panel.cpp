// Polyhymnia - Clips list panel on the right (after SoundCraft crates/ui-egui/src/panels/clips.rs).
#include "clips_panel.h"
#include "app.h"
#include "editor.h"
#include "engine.h"
#include "lang.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <set>

namespace ClipsPanel {

namespace {

constexpr ImU32 kColRowSelected = IM_COL32(0x00, 0x5c, 0x8a, 180);
constexpr ImU32 kColRowHover = IM_COL32(0x2e, 0x2e, 0x2e, 255);
constexpr ImU32 kColText = IM_COL32(0xd6, 0xd6, 0xd6, 255);
constexpr ImU32 kColSourceSwatch = IM_COL32(150, 150, 150, 255);

// after SoundCraft (crates/ui-egui/src/panels/clips.rs ClipRows)
struct CachedClipRows {
    uint32_t revision = 0;
    size_t sourceCount = 0;
    size_t clipCount = 0;
    std::string filter;
    std::vector<ClipRow> rows;
};

static CachedClipRows gCachedRows;
static char gFilterBuf[128] = "";
static ImVec2 gPanelMin = ImVec2(0, 0);
static ImVec2 gPanelMax = ImVec2(0, 0);

struct ClearModalState {
    bool open = false;
    std::vector<uint64_t> clips;
    std::vector<uint64_t> sources;
    std::string message;
};

struct RenameClipModalState {
    bool open = false;
    uint64_t clipNumId = 0;
    char nameBuf[256] = "";
};

static ClearModalState gClearModal;
static RenameClipModalState gRenameModal;

std::string toLower(const std::string& s) {
    std::string out = s;
    for (char& ch : out) ch = (char)std::tolower((unsigned char)ch);
    return out;
}

std::string sourceDisplayName(const Session::Source& src) {
    std::string base = src.name;
    if (base.empty()) {
        base = std::filesystem::u8path(src.path).stem().u8string();
        if (base.empty()) base = src.path.empty() ? src.id : src.path;
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s (%dch)", base.c_str(), std::max(1, src.channels));
    return buf;
}

} // namespace

// after SoundCraft (crates/ui-egui/src/panels/clips.rs clip_rows)
std::vector<ClipRow> clipRows(const Session::Document& s, const std::string& needleRaw) {
    const std::string needle = toLower(needleRaw);
    auto shown = [&](const std::string& name) {
        return needle.empty() || toLower(name).find(needle) != std::string::npos;
    };
    std::vector<ClipRow> out;
    for (size_t i = 0; i < s.sources.size(); ++i) {
        if (shown(sourceDisplayName(s.sources[i]))) {
            out.push_back(ClipRow::source(i));
        }
    }
    for (size_t tr = 0; tr < s.tracks.size(); ++tr) {
        const auto& clips = s.tracks[tr].activeClips();
        for (size_t cl = 0; cl < clips.size(); ++cl) {
            if (shown(clips[cl].name)) {
                out.push_back(ClipRow::clip(tr, cl));
            }
        }
    }
    return out;
}

// after SoundCraft (crates/ui-egui/src/dialogs.rs clear_message)
std::optional<std::string> clearMessage(const Session::Document& s, const std::vector<uint64_t>& clips,
                                        const std::vector<uint64_t>& sources) {
    const std::set<uint64_t> listed(clips.begin(), clips.end());
    std::vector<std::string> names;
    for (const auto& t : s.tracks) {
        for (const auto& c : t.activeClips()) {
            if (listed.count(c.numId)) names.push_back(c.name);
        }
    }
    std::vector<const Session::Source*> files;
    for (uint64_t id : sources) {
        if (const auto* f = Session::findSourceByNum(s, id)) files.push_back(f);
    }
    auto countStr = [](size_t n, const char* what) {
        return n == 1 ? ("1 " + std::string(what)) : (std::to_string(n) + " " + std::string(what) + "s");
    };
    std::string what;
    if (names.empty() && files.empty()) return std::nullopt;
    if (names.size() == 1 && files.empty()) {
        what = "the clip \"" + names[0] + "\"";
    } else if (names.empty() && files.size() == 1) {
        const std::string fn = files[0]->name.empty() ? files[0]->path : files[0]->name;
        what = "the file \"" + fn + "\"";
    } else if (!names.empty() && files.empty()) {
        what = countStr(names.size(), "clip");
    } else if (names.empty() && !files.empty()) {
        what = countStr(files.size(), "file");
    } else {
        what = countStr(names.size(), "clip") + " and " + countStr(files.size(), "file");
    }

    std::string text = "Remove " + what + " from the session?";
    std::vector<std::pair<std::string, size_t>> used;
    size_t totalUsers = 0;
    for (const auto* f : files) {
        size_t n = 0;
        for (const auto* c : Session::clipsUsing(s, f->numId)) {
            if (!listed.count(c->numId)) ++n;
        }
        if (n > 0) {
            used.emplace_back(f->name.empty() ? f->path : f->name, n);
            totalUsers += n;
        }
    }
    const std::string users = (totalUsers == 1)
                                  ? "a clip on the timeline, which is removed too."
                                  : (std::to_string(totalUsers) + " clips on the timeline, which are removed too.");
    if (!used.empty()) {
        if (used.size() == 1 && files.size() == 1) {
            text += " It is used by " + users;
        } else if (used.size() == files.size()) {
            text += " They are used by " + users;
        } else if (used.size() == 1) {
            text += " \"" + used[0].first + "\" is used by " + users;
        } else {
            text += " " + std::to_string(used.size()) + " of the files are used by " + users;
        }
    }
    bool allSaved = !files.empty();
    for (const auto* f : files) {
        if (f->unsaved) allSaved = false;
    }
    if (allSaved) {
        text += (files.size() == 1) ? " The file stays on disk." : " The files stay on disk.";
    }
    return text;
}

bool containsScreenPoint(float x, float y) {
    return x >= gPanelMin.x && x <= gPanelMax.x && y >= gPanelMin.y && y <= gPanelMax.y;
}

// after SoundCraft (crates/ui-egui/src/panels/clips.rs clip_list)
void draw(Session::Document& doc, float width) {
    ImGui::BeginChild("clips_panel", ImVec2(width, 0), true);
    gPanelMin = ImGui::GetWindowPos();
    gPanelMax = ImVec2(gPanelMin.x + ImGui::GetWindowSize().x, gPanelMin.y + ImGui::GetWindowSize().y);

    ImGui::TextUnformatted(T("CLIPS"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##clip_filter", "Find clips", gFilterBuf, sizeof(gFilterBuf));
    const std::string needle = toLower(gFilterBuf);

    size_t totalClips = 0;
    for (const auto& t : doc.tracks) totalClips += t.activeClips().size();
    if (gCachedRows.revision != doc.touch || gCachedRows.sourceCount != doc.sources.size() ||
        gCachedRows.clipCount != totalClips || gCachedRows.filter != needle) {
        gCachedRows.revision = doc.touch;
        gCachedRows.sourceCount = doc.sources.size();
        gCachedRows.clipCount = totalClips;
        gCachedRows.filter = needle;
        gCachedRows.rows = clipRows(doc, needle);
    }

    struct RowDisplay {
        uint64_t numId = 0;
        std::string trackId;
        std::string clipId;
        std::string name;
        bool whole = false;
        std::array<uint8_t, 3> color = {150, 150, 150};
        bool selected = false;
    };

    std::vector<RowDisplay> items;
    items.reserve(gCachedRows.rows.size());
    for (const ClipRow& row : gCachedRows.rows) {
        if (row.kind == ClipRow::Kind::Source) {
            if (row.sourceIdx >= doc.sources.size()) continue;
            const Session::Source& src = doc.sources[row.sourceIdx];
            const uint64_t sid = src.numId != 0 ? src.numId : static_cast<uint64_t>(row.sourceIdx + 1);
            const bool sel = std::find(doc.sessionEdit.selectedSources.begin(),
                                       doc.sessionEdit.selectedSources.end(), sid) != doc.sessionEdit.selectedSources.end();
            items.push_back({sid, {}, {}, sourceDisplayName(src), true, {150, 150, 150}, sel});
        } else {
            if (row.trackIdx >= doc.tracks.size()) continue;
            const Session::Track& tr = doc.tracks[row.trackIdx];
            const auto& clips = tr.activeClips();
            if (row.clipIdx >= clips.size()) continue;
            const Session::Clip& c = clips[row.clipIdx];
            bool sel = std::find(doc.sessionEdit.selectedClips.begin(),
                                 doc.sessionEdit.selectedClips.end(), c.numId) != doc.sessionEdit.selectedClips.end();
            for (const auto& r : Editor::selection().clips) {
                if (r.trackId == tr.id && r.clipId == c.id) sel = true;
            }
            const std::array<uint8_t, 3> col = c.color.value_or(tr.color);
            items.push_back({c.numId, tr.id, c.id, c.name.empty() ? c.id : c.name, false, col, sel});
        }
    }

    ImGui::BeginChild("clips_scroll", ImVec2(0, 0), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH = 19.0f;

    for (size_t idx = 0; idx < items.size(); ++idx) {
        const RowDisplay& item = items[idx];
        ImGui::PushID(static_cast<int>(idx));
        const ImVec2 rMin = ImGui::GetCursorScreenPos();
        const float availW = std::max(10.0f, ImGui::GetContentRegionAvail().x);
        const ImVec2 rMax(rMin.x + availW, rMin.y + rowH);
        ImGui::InvisibleButton("##clip_row", ImVec2(availW, rowH));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        if (item.selected) {
            dl->AddRectFilled(rMin, rMax, kColRowSelected);
        } else if (hovered) {
            dl->AddRectFilled(rMin, rMax, kColRowHover);
        }

        const ImU32 swatchCol = item.whole
                                    ? kColSourceSwatch
                                    : IM_COL32(item.color[0], item.color[1], item.color[2], 255);
        dl->AddRectFilled(ImVec2(rMin.x + 6.0f, rMin.y + 5.0f), ImVec2(rMin.x + 15.0f, rMin.y + 14.0f), swatchCol, 1.0f);
        dl->AddText(ImVec2(rMin.x + 22.0f, rMin.y + 2.0f), kColText, item.name.c_str());
        if (item.whole) {
            // Bold effect for whole-file source rows (as in SoundCraft panels/clips.rs).
            dl->AddText(ImVec2(rMin.x + 22.6f, rMin.y + 2.0f), kColText, item.name.c_str());
        }

        if (item.whole && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            const uint64_t payloadId = item.numId;
            ImGui::SetDragDropPayload(kDragSourcePayload, &payloadId, sizeof(uint64_t));
            ImGui::TextUnformatted(item.name.c_str());
            ImGui::EndDragDropSource();
        }

        if (clicked) {
            if (item.whole) {
                doc.sessionEdit.selectedSources = {item.numId};
                doc.sessionEdit.selectedClips.clear();
                Editor::clearSelection();
            } else {
                doc.sessionEdit.selectedClips = {item.numId};
                doc.sessionEdit.selectedSources.clear();
                Editor::selectClip(doc, item.trackId, item.clipId);
            }
        }

        if (ImGui::BeginPopupContextItem("clip_row_ctx")) {
            if (item.whole) {
                if (ImGui::MenuItem("Place on New Track")) {
                    const uint64_t srcId = item.numId;
                    App::mutateDocument("Place Audio File", [srcId](Session::Document& d) {
                        EngCmd::Engine e;
                        e.doc = std::move(d);
                        EngCmd::Engine::Params p;
                        p.nums["source"] = static_cast<double>(srcId);
                        e.execute("clip.place_source", p);
                        d = std::move(e.doc);
                    });
                }
                std::optional<uint64_t> selTrack;
                if (!doc.sessionEdit.selectedTracks.empty()) {
                    const Session::Track* tr = Session::findTrackByNum(doc, doc.sessionEdit.selectedTracks.front());
                    if (tr && tr->kind == Session::TrackKind::Audio) selTrack = tr->numId;
                } else if (!Editor::selection().trackId.empty()) {
                    const Session::Track* tr = Session::findTrack(doc, Editor::selection().trackId);
                    if (tr && tr->kind == Session::TrackKind::Audio) selTrack = tr->numId;
                }
                if (selTrack && ImGui::MenuItem("Place on Selected Track")) {
                    const uint64_t srcId = item.numId;
                    const uint64_t trId = *selTrack;
                    App::mutateDocument("Place Audio File", [srcId, trId](Session::Document& d) {
                        EngCmd::Engine e;
                        e.doc = std::move(d);
                        EngCmd::Engine::Params p;
                        p.nums["source"] = static_cast<double>(srcId);
                        p.nums["track"] = static_cast<double>(trId);
                        e.execute("clip.place_source", p);
                        d = std::move(e.doc);
                    });
                }
                ImGui::Separator();
            }
            if (!item.whole && ImGui::MenuItem("Rename...")) {
                gRenameModal.open = true;
                gRenameModal.clipNumId = item.numId;
                std::snprintf(gRenameModal.nameBuf, sizeof(gRenameModal.nameBuf), "%s", item.name.c_str());
            }
            if (ImGui::MenuItem("Clear...")) {
                std::vector<uint64_t> clips;
                std::vector<uint64_t> sources;
                if (item.whole) {
                    sources.push_back(item.numId);
                } else {
                    clips.push_back(item.numId);
                }
                if (auto msg = clearMessage(doc, clips, sources)) {
                    gClearModal.open = true;
                    gClearModal.clips = std::move(clips);
                    gClearModal.sources = std::move(sources);
                    gClearModal.message = std::move(*msg);
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::EndChild();
}

void drawDialogs(Session::Document& doc) {
    if (gClearModal.open) {
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Clear", &gClearModal.open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextWrapped("%s", gClearModal.message.c_str());
            ImGui::Separator();
            if (ImGui::Button("Cancel")) {
                gClearModal.open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                const auto clips = gClearModal.clips;
                const auto sources = gClearModal.sources;
                App::mutateDocument("Clear Clips", [clips, sources](Session::Document& d) {
                    EngCmd::Engine e;
                    e.doc = std::move(d);
                    EngCmd::Engine::Params p;
                    if (!clips.empty()) p.idLists["clips"] = clips;
                    if (!sources.empty()) p.idLists["sources"] = sources;
                    p.bools["with_clips"] = true;
                    e.execute("clip.clear", p);
                    d = std::move(e.doc);
                });
                gClearModal.open = false;
            }
        }
        ImGui::End();
    }

    if (gRenameModal.open) {
        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Rename Clip", &gRenameModal.open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            ImGui::InputText("Name", gRenameModal.nameBuf, sizeof(gRenameModal.nameBuf));
            ImGui::Separator();
            if (ImGui::Button("Cancel")) {
                gRenameModal.open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("OK")) {
                const uint64_t cid = gRenameModal.clipNumId;
                const std::string newName = gRenameModal.nameBuf;
                App::mutateDocument("Rename Clip", [cid, newName](Session::Document& d) {
                    if (auto f = Session::findClipByNum(d, cid)) {
                        f->second->name = newName;
                    }
                });
                gRenameModal.open = false;
            }
        }
        ImGui::End();
    }
}

} // namespace ClipsPanel
