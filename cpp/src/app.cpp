// Polyhymnia - Edit window shell, menus, docks, dialogs, preferences, test hooks
// (after SoundCraft crates/ui-egui/src/menus.rs, edit_window.rs, panels/*.rs, dialogs.rs).
#include "app.h"
#include "audio.h"
#include "clip_ops.h"
#include "clips_panel.h"
#include "edit.h"
#include "editor.h"
#include "engine.h"
#include "icons.h"
#include "import_audio.h"
#include "lang.h"
#include "mix.h"
#include "mix_window.h"
#include "playback.h"
#include "project.h"
#include "record_bounce.h"
#include "splash.h"
#include "spot_dialog.h"
#include "time_format.h"
#include "undo.h"

#include <imgui.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifndef POLYHYMNIA_VERSION
#define POLYHYMNIA_VERSION "0.1.0"
#endif

namespace App {

static SDL_Window* mainWin = nullptr;
static std::string prefDir, langDir;
static bool quit = false;
static bool showPrefs = false;
static int prefsPage = 0;
static bool showAbout = false;
static int langIdx = 0;
static int devIdx = -1;
static float bufferSlider = 512;
static std::string audioNote;
static std::map<std::string, std::string> prefs;
static Session::Document doc;
static bool hasDoc = false;
static std::string docPath;
static std::string sessionNote;
static Undo::History hist;
static ImportAudio::Imports gImports;

// after SoundCraft (crates/ui-egui/src/shortcuts.rs): this table supplies both menu labels and key dispatch.
struct ShortcutDef { const char* id; const char* label; ImGuiKey key; bool ctrl, shift, alt; };
static const ShortcutDef kShortcuts[] = {
    {"new", "Ctrl+N", ImGuiKey_N, true, false, false}, {"save", "Ctrl+S", ImGuiKey_S, true, false, false},
    {"undo", "Ctrl+Z", ImGuiKey_Z, true, false, false}, {"redo", "Ctrl+Shift+Z", ImGuiKey_Z, true, true, false},
    {"cut", "Ctrl+X", ImGuiKey_X, true, false, false}, {"copy", "Ctrl+C", ImGuiKey_C, true, false, false},
    {"paste", "Ctrl+V", ImGuiKey_V, true, false, false}, {"clear", "Del", ImGuiKey_Delete, false, false, false},
    {"duplicate", "Ctrl+D", ImGuiKey_D, true, false, false}, {"separate", "Ctrl+E", ImGuiKey_E, true, false, false},
    {"heal", "Ctrl+H", ImGuiKey_H, true, false, false}, {"mute", "Ctrl+M", ImGuiKey_M, true, false, false},
    {"fades", "Ctrl+F", ImGuiKey_F, true, false, false}, {"fade_start", "Alt+D", ImGuiKey_D, false, false, true},
    {"fade_end", "Alt+G", ImGuiKey_G, false, false, true}, {"loop", "Ctrl+Shift+L", ImGuiKey_L, true, true, false},
    {"click", "K", ImGuiKey_K, false, false, false}, {"marker", "Enter", ImGuiKey_Enter, false, false, false},
    {"import", "Ctrl+Shift+I", ImGuiKey_I, true, true, false}, {"new_tracks", "Ctrl+Shift+N", ImGuiKey_N, true, true, false},
    {"bounce", "Ctrl+Alt+B", ImGuiKey_B, true, false, true}, {"export_clips", "Ctrl+Shift+K", ImGuiKey_K, true, true, false},
    {"duplicate_track", "Alt+Shift+D", ImGuiKey_D, false, true, true}, {"group", "Ctrl+G", ImGuiKey_G, true, false, false},
    {"narrow_mix", "Ctrl+Alt+M", ImGuiKey_M, true, false, true}, {"window", "Ctrl+=", ImGuiKey_Equal, true, false, false},
    {"preferences", "Ctrl+,", ImGuiKey_Comma, true, false, false}, {"quit", "Alt+F4", ImGuiKey_F4, false, false, true}
};
static const char* shortcutLabel(const char* id) {
    for (const auto& s : kShortcuts) if (std::strcmp(s.id, id) == 0) return s.label;
    return nullptr;
}

// Layout dock state (after SoundCraft crates/ui-egui/src/state.rs & edit_window.rs)
static int gMainWindow = 0; // 0 = Edit, 1 = Mix
static bool gShowLeftDock = true;
static bool gShowRightDock = true;
static float gLeftDockW = 190.0f;
static float gRightDockW = 220.0f;
static int gLowerTab = -1; // -1 = collapsed, 0 = MIDI EDITOR, 1 = MEMORY LOCATIONS, 2 = UNDO HISTORY
static float gLowerDockH = 190.0f;

struct ImportAudioDialogState {
    bool open = false;
    char pathBuf[1024] = "";
    std::vector<std::string> files;
    int destMode = 0; // 0 = New Track, 1 = Selected Track, 2 = Clip List Only
    int locMode = 0;  // 0 = Session Start, 1 = Selection / Playhead, 2 = Spot
    bool copyToAudioFiles = true;
};
static ImportAudioDialogState gImportDlg;

// New Tracks dialog state (after SoundCraft crates/ui-egui/src/dialogs/new_tracks.rs)
struct NewTrackRowUI {
    int count = 1;
    int formatIdx = 1; // 0 = Mono, 1 = Stereo
    int kindIdx = 0;   // 0 = Audio, 1 = Aux, 2 = Master, 3 = MIDI, 4 = Instrument, 5 = VCA, 6 = Folder
    int timebaseIdx = 0; // 0 = Samples, 1 = Ticks
    char nameBuf[128] = "Audio";
};
struct NewTracksDialogState {
    bool open = false;
    std::vector<NewTrackRowUI> rows;
};
static NewTracksDialogState gNewTracksDlg;

static const ImU32 C_BG0 = IM_COL32(0x16, 0x16, 0x16, 255);
static const ImU32 C_BG1 = IM_COL32(0x1e, 0x1e, 0x1e, 255);
static const ImU32 C_BG2 = IM_COL32(0x25, 0x25, 0x25, 255);
static const ImU32 C_BG3 = IM_COL32(0x2e, 0x2e, 0x2e, 255);
static const ImU32 C_BORDER = IM_COL32(0x0c, 0x0c, 0x0c, 255);
static const ImU32 C_LINE = IM_COL32(0x33, 0x33, 0x33, 255);
static const ImU32 C_TEXT = IM_COL32(0xe2, 0xe2, 0xe2, 255);
static const ImU32 C_DIM = IM_COL32(0x96, 0x96, 0x96, 255);
static const ImU32 C_MUTED = IM_COL32(0x64, 0x64, 0x64, 255);
static const ImU32 C_BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255);
static const ImU32 C_DEEP = IM_COL32(0x00, 0x5c, 0x8a, 255);

static ImVec4 col4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// Atelier greys and blue everywhere (after Mosaic Mask gui.cpp applyStyle & SoundCraft theme.rs); no default ImGui navy.
static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0;
    s.FrameRounding = 2;
    s.GrabRounding = 2;
    s.PopupRounding = 2;
    s.ScrollbarRounding = 2;
    s.TabRounding = 2;
    s.WindowBorderSize = 0;
    s.FrameBorderSize = 0;
    s.PopupBorderSize = 1;
    s.WindowPadding = ImVec2(8, 6);
    s.FramePadding = ImVec2(6, 3);
    s.ItemSpacing = ImVec2(6, 5);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col4(C_TEXT);
    c[ImGuiCol_TextDisabled] = col4(C_DIM);
    c[ImGuiCol_WindowBg] = col4(C_BG1);
    c[ImGuiCol_ChildBg] = col4(C_BG1);
    c[ImGuiCol_PopupBg] = col4(C_BG2);
    c[ImGuiCol_Border] = col4(C_LINE);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Separator] = col4(C_LINE);
    c[ImGuiCol_SeparatorHovered] = col4(C_DEEP);
    c[ImGuiCol_SeparatorActive] = col4(C_BLUE);
    c[ImGuiCol_FrameBg] = col4(C_BG2);
    c[ImGuiCol_FrameBgHovered] = col4(C_BG3);
    c[ImGuiCol_FrameBgActive] = col4(IM_COL32(0x36, 0x36, 0x36, 255));
    c[ImGuiCol_TitleBg] = col4(C_BG0);
    c[ImGuiCol_TitleBgActive] = col4(C_BG0);
    c[ImGuiCol_TitleBgCollapsed] = col4(C_BG0);
    c[ImGuiCol_MenuBarBg] = col4(C_BG1);
    c[ImGuiCol_ScrollbarBg] = col4(C_BG1);
    c[ImGuiCol_ScrollbarGrab] = col4(C_LINE);
    c[ImGuiCol_ScrollbarGrabHovered] = col4(C_DEEP);
    c[ImGuiCol_ScrollbarGrabActive] = col4(C_BLUE);
    c[ImGuiCol_CheckMark] = col4(C_BLUE);
    c[ImGuiCol_SliderGrab] = col4(C_BLUE);
    c[ImGuiCol_SliderGrabActive] = col4(C_DEEP);
    c[ImGuiCol_Button] = col4(C_BG3);
    c[ImGuiCol_ButtonHovered] = col4(C_DEEP);
    c[ImGuiCol_ButtonActive] = col4(C_BLUE);
    c[ImGuiCol_Header] = col4(C_BG3);
    c[ImGuiCol_HeaderHovered] = col4(C_DEEP);
    c[ImGuiCol_HeaderActive] = col4(C_BLUE);
    c[ImGuiCol_Tab] = col4(C_BG0);
    c[ImGuiCol_TabHovered] = col4(C_DEEP);
    c[ImGuiCol_TabActive] = col4(C_BG2);
    c[ImGuiCol_TabUnfocused] = col4(C_BG0);
    c[ImGuiCol_TabUnfocusedActive] = col4(C_BG2);
    c[ImGuiCol_ResizeGrip] = col4(C_LINE);
    c[ImGuiCol_ResizeGripHovered] = col4(C_DEEP);
    c[ImGuiCol_ResizeGripActive] = col4(C_BLUE);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
}

static void loadPrefs() {
    std::ifstream f(prefDir + "polyhymnia.ini");
    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos) prefs[line.substr(0, eq)] = line.substr(eq + 1);
    }
    if (auto it = prefs.find("lang"); it != prefs.end()) {
        const auto& packs = Lang::packs();
        for (size_t i = 0; i < packs.size(); ++i)
            if (packs[i].code == it->second) langIdx = (int)i;
    }
    if (auto it = prefs.find("buffer"); it != prefs.end()) bufferSlider = (float)std::stoi(it->second);
    if (auto it = prefs.find("audio"); it != prefs.end()) devIdx = std::stoi(it->second);
}

static void savePrefs() {
    std::ofstream f(prefDir + "polyhymnia.ini");
    f << "lang=" << Lang::current() << "\n";
    f << "buffer=" << (int)bufferSlider << "\n";
    if (devIdx >= 0) f << "audio=" << devIdx << "\n";
}

static void applyAudioDevice() {
    if (Audio::open(devIdx, (unsigned)bufferSlider)) {
        audioNote = T("Audio device open") + std::string(": ") + Audio::activeDeviceName();
    } else {
        audioNote = T("Could not open the audio device");
    }
}

static void newDoc() {
    doc = Session::empty();
    doc.folder = prefDir;
    hasDoc = true;
    docPath.clear();
    sessionNote.clear();
    hist.clear();
    Editor::clearSelection();
    Playback::setDocument(&doc);
}

static void openDemoDoc() {
    doc = Session::midnightGrooveDemo();
    doc.folder = prefDir;
    hasDoc = true;
    docPath.clear();
    sessionNote.clear();
    hist.clear();
    Edit::scriptSetup(doc);
    Playback::setDocument(&doc);
}

static void openNewTracksDialog() {
    if (!hasDoc) newDoc();
    gNewTracksDlg.open = true;
    if (gNewTracksDlg.rows.empty()) {
        gNewTracksDlg.rows.push_back(NewTrackRowUI{});
    }
}

static std::vector<std::string> pickAudioFilesDialog() {
    std::vector<std::string> out;
#ifdef _WIN32
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = L"Audio Files (*.wav;*.flac;*.mp3;*.aif;*.aiff;*.aifc;*.ogg)\0*.wav;*.flac;*.mp3;*.aif;*.aiff;*.aifc;*.ogg\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        const wchar_t* p = buf.data();
        std::filesystem::path first(p);
        p += wcslen(p) + 1;
        if (*p == 0) {
            out.push_back(first.u8string());
        } else {
            while (*p) {
                std::filesystem::path file = first / p;
                out.push_back(file.u8string());
                p += wcslen(p) + 1;
            }
        }
    }
#endif
    return out;
}

static void openImportAudioDialog() {
    if (!hasDoc) newDoc();
    gImportDlg.open = true;
    if (gImportDlg.pathBuf[0] == '\0' && !doc.folder.empty()) {
        std::snprintf(gImportDlg.pathBuf, sizeof(gImportDlg.pathBuf), "%s", doc.folder.c_str());
    }
}

static void runImportPaths(const std::vector<std::string>& paths, int destMode, int locMode, bool copyToAudioFiles,
                           std::optional<uint64_t> explicitTrack = std::nullopt, std::optional<long long> explicitAt = std::nullopt) {
    if (paths.empty()) return;
    if (!hasDoc) newDoc();
    if (doc.folder.empty()) doc.folder = prefDir;

    long long at = 0;
    if (explicitAt.has_value()) {
        at = *explicitAt;
    } else if (locMode == 1) {
        at = doc.sessionEdit.selection.start > 0 ? doc.sessionEdit.selection.start : Playback::playhead();
    }

    std::optional<uint64_t> targetTrack = explicitTrack;
    if (!targetTrack && destMode == 1) {
        if (!doc.sessionEdit.selectedTracks.empty()) {
            const Session::Track* tr = Session::findTrackByNum(doc, doc.sessionEdit.selectedTracks.front());
            if (tr && tr->kind == Session::TrackKind::Audio) targetTrack = tr->numId;
        } else if (!Editor::selection().trackId.empty()) {
            const Session::Track* tr = Session::findTrack(doc, Editor::selection().trackId);
            if (tr && tr->kind == Session::TrackKind::Audio) targetTrack = tr->numId;
        }
    }

    const bool clipsListOnly = (destMode == 2) || (locMode == 2);
    uint64_t lastSourceNum = 0;
    uint64_t lastTrackNum = targetTrack.value_or(0);
    std::string importErr;

    mutateDocument("Import Audio", [&](Session::Document& d) {
        EngCmd::Engine e;
        e.doc = std::move(d);
        EngCmd::Engine::Params p;
        p.strLists["paths"] = paths;
        p.nums["at"] = static_cast<double>(at);
        p.bools["copy_to_audio_files"] = copyToAudioFiles;
        p.bools["clips_list_only"] = clipsListOnly;
        if (targetTrack) {
            p.nums["track"] = static_cast<double>(*targetTrack);
            p.bools["new_tracks"] = false;
        } else {
            p.bools["new_tracks"] = (destMode != 1);
        }
        const auto r = e.execute("file.import_audio", p);
        if (!r.ok) {
            importErr = r.error;
        } else {
            lastSourceNum = static_cast<uint64_t>(r.nums.count("source") ? r.nums.at("source") : 0.0);
            if (r.nums.count("track")) lastTrackNum = static_cast<uint64_t>(r.nums.at("track"));
        }
        d = std::move(e.doc);
    });

    if (!importErr.empty()) {
        sessionNote = importErr;
    } else {
        const std::string firstName = std::filesystem::u8path(paths.front()).filename().u8string();
        sessionNote = paths.size() == 1 ? ("Imported " + firstName)
                                        : ("Imported " + std::to_string(paths.size()) + " audio files");
        if (locMode == 2 && lastSourceNum != 0) {
            auto spot = Spot::source(doc, lastSourceNum, lastTrackNum, at);
            if (spot) Spot::open(std::move(*spot));
        }
    }
}

void onFileDrop(const std::string& path, float x, float y) {
    if (path.empty()) return;
    std::string ext = std::filesystem::u8path(path).extension().u8string();
    for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
    if (ext == ".polyhymnia") {
        std::string err;
        Session::Document loaded;
        if (Project::load(path, loaded, err)) {
            doc = std::move(loaded);
            hasDoc = true;
            docPath = path;
            hist.clear();
            Playback::setDocument(&doc);
            sessionNote = "Opened " + std::filesystem::u8path(path).filename().u8string();
        } else {
            sessionNote = err;
        }
        return;
    }

    if (!hasDoc) newDoc();
    if (ClipsPanel::containsScreenPoint(x, y)) {
        runImportPaths({path}, 2, 0, true);
        return;
    }

    const Edit::DropTarget hit = Edit::hitTestDrop(doc, x, y);
    if (hit.inTimeline && hit.trackNumId != 0) {
        const int locMode = (Editor::mode() == Editor::Mode::Spot) ? 2 : 0;
        runImportPaths({path}, 1, locMode, true, hit.trackNumId, hit.sampleAt);
    } else if (hit.inTimeline) {
        const int locMode = (Editor::mode() == Editor::Mode::Spot) ? 2 : 0;
        runImportPaths({path}, 0, locMode, true, std::nullopt, hit.sampleAt);
    } else {
        runImportPaths({path}, 0, 1, true);
    }
}

static void drawImportAudioDialog() {
    if (!gImportDlg.open) return;
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(T("Import Audio"), &gImportDlg.open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted(T("Audio files (WAV, FLAC, MP3, AIFF, OGG):"));
        ImGui::SetNextItemWidth(390.0f);
        const bool enterPressed = ImGui::InputText("##import_path", gImportDlg.pathBuf, sizeof(gImportDlg.pathBuf),
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button(T("Browse..."))) {
            auto picked = pickAudioFilesDialog();
            if (!picked.empty()) {
                gImportDlg.files = picked;
                std::snprintf(gImportDlg.pathBuf, sizeof(gImportDlg.pathBuf), "%s", picked.front().c_str());
            }
        }
        if (gImportDlg.files.size() > 1) {
            ImGui::Text("%zu files selected for import", gImportDlg.files.size());
            ImGui::SameLine();
            if (ImGui::SmallButton(T("Clear list"))) {
                gImportDlg.files.clear();
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted(T("Destination:"));
        ImGui::RadioButton(T("New Track"), &gImportDlg.destMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton(T("Selected Track"), &gImportDlg.destMode, 1);
        ImGui::SameLine();
        ImGui::RadioButton(T("Clip List Only"), &gImportDlg.destMode, 2);

        ImGui::TextUnformatted(T("Location:"));
        ImGui::RadioButton(T("Session Start"), &gImportDlg.locMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton(T("Selection / Cursor"), &gImportDlg.locMode, 1);
        ImGui::SameLine();
        ImGui::RadioButton(T("Spot"), &gImportDlg.locMode, 2);

        ImGui::Checkbox(T("Copy into project's Audio Files folder"), &gImportDlg.copyToAudioFiles);

        ImGui::Separator();
        if (ImGui::Button(T("Cancel"))) {
            gImportDlg.open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(T("Import")) || enterPressed) {
            std::vector<std::string> toImport = gImportDlg.files;
            if (toImport.empty() && gImportDlg.pathBuf[0] != '\0') {
                toImport.push_back(gImportDlg.pathBuf);
            }
            if (!toImport.empty()) {
                runImportPaths(toImport, gImportDlg.destMode, gImportDlg.locMode, gImportDlg.copyToAudioFiles);
                gImportDlg.files.clear();
                gImportDlg.open = false;
            }
        }
    }
    ImGui::End();
}

static void drawNewTracksDialog() {
    if (!gNewTracksDlg.open) return;
    ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(T("New Tracks"), &gNewTracksDlg.open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        static const char* kFormats[] = {"Mono", "Stereo"};
        static const char* kKinds[] = {"Audio Track", "Aux Input", "Master Fader", "MIDI Track", "Instrument Track", "VCA Master", "Folder Track"};
        static const char* kKindIds[] = {"audio", "aux", "master", "midi", "instrument", "vca", "folder"};
        static const char* kDefaultNames[] = {"Audio", "Aux", "Master", "MIDI", "Inst", "VCA", "Folder"};
        static const char* kTimebases[] = {"Samples", "Ticks"};

        int addAfter = -1, removeIdx = -1;
        for (size_t i = 0; i < gNewTracksDlg.rows.size(); ++i) {
            ImGui::PushID((int)i);
            auto& r = gNewTracksDlg.rows[i];
            ImGui::TextUnformatted("Create");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(52.0f);
            ImGui::DragInt("##cnt", &r.count, 0.2f, 1, 128);
            r.count = std::clamp(r.count, 1, 128);
            ImGui::SameLine();
            ImGui::TextUnformatted("new");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(82.0f);
            ImGui::Combo("##fmt", &r.formatIdx, kFormats, 2);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130.0f);
            const int prevKind = r.kindIdx;
            if (ImGui::Combo("##kind", &r.kindIdx, kKinds, 7)) {
                if (std::strcmp(r.nameBuf, kDefaultNames[prevKind]) == 0) {
                    std::snprintf(r.nameBuf, sizeof(r.nameBuf), "%s", kDefaultNames[r.kindIdx]);
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("in");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(84.0f);
            ImGui::Combo("##tb", &r.timebaseIdx, kTimebases, 2);
            ImGui::SameLine();
            ImGui::TextUnformatted("Name:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            ImGui::InputText("##name", r.nameBuf, sizeof(r.nameBuf));
            ImGui::SameLine();
            ImGui::BeginDisabled(gNewTracksDlg.rows.size() <= 1);
            if (ImGui::Button("-", ImVec2(22, 0))) removeIdx = (int)i;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("+", ImVec2(22, 0))) addAfter = (int)i;
            ImGui::PopID();
        }
        if (removeIdx >= 0 && gNewTracksDlg.rows.size() > 1) {
            gNewTracksDlg.rows.erase(gNewTracksDlg.rows.begin() + removeIdx);
        } else if (addAfter >= 0 && gNewTracksDlg.rows.size() < 128) {
            gNewTracksDlg.rows.insert(gNewTracksDlg.rows.begin() + addAfter + 1, NewTrackRowUI{});
        }

        ImGui::Separator();
        if (ImGui::Button(T("Cancel"))) {
            gNewTracksDlg.open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(T("Create")) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            const auto rowsCopy = gNewTracksDlg.rows;
            mutateDocument("New Tracks", [rowsCopy](Session::Document& d) {
                EngCmd::Engine e;
                e.doc = std::move(d);
                for (const auto& r : rowsCopy) {
                    EngCmd::Engine::Params p;
                    p.nums["count"] = (double)r.count;
                    p.strs["kind"] = kKindIds[r.kindIdx];
                    p.strs["format"] = r.formatIdx == 0 ? "Mono" : "Stereo";
                    p.strs["name"] = r.nameBuf;
                    e.execute("track.new", p);
                }
                d = std::move(e.doc);
            });
            gNewTracksDlg.open = false;
        }
    }
    ImGui::End();
}

static void saveDoc() {
    if (!hasDoc) return;
    if (docPath.empty()) docPath = (std::filesystem::u8path(prefDir) / "Untitled.polyhymnia").u8string();
    std::string err;
    if (Project::save(doc, docPath, err)) sessionNote = T("Saved");
    else sessionNote = err;
}

// ============================================================================
// Left Dock: TRACKS & GROUPS (after SoundCraft crates/ui-egui/src/panels/tracks.rs)
// ============================================================================

static void drawLeftTracksAndGroupsDock(float width) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::BeginChild("sc_left_dock", ImVec2(width, 0), true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float tracksSectionH = std::max(100.0f, avail.y * 0.64f);

    // TRACKS header + '+' button
    ImGui::TextColored(col4(C_DIM), "TRACKS");
    ImGui::SameLine(avail.x - 18.0f);
    if (Icons::iconButton("left_add_trk", ImVec2(18.0f, 16.0f), "plus", false, "New Tracks (Ctrl+Shift+N)", C_BLUE)) {
        openNewTracksDialog();
    }
    ImGui::Separator();

    ImGui::BeginChild("tracks_list_scroll", ImVec2(0, tracksSectionH - 28.0f), false);
    for (size_t i = 0; i < doc.tracks.size(); ++i) {
        auto& t = doc.tracks[i];
        ImGui::PushID((int)i);
        const ImVec2 rMin = ImGui::GetCursorScreenPos();
        const float rW = ImGui::GetContentRegionAvail().x;
        const float rH = 20.0f;
        const bool isSel = std::find(doc.sessionEdit.selectedTracks.begin(), doc.sessionEdit.selectedTracks.end(), t.numId) != doc.sessionEdit.selectedTracks.end()
                        || Editor::selection().trackId == t.id;

        if (isSel) {
            dl->AddRectFilled(rMin, ImVec2(rMin.x + rW, rMin.y + rH), C_BG3, 2.0f);
        }

        // Visibility dot button
        ImGui::SetCursorScreenPos(ImVec2(rMin.x + 2.0f, rMin.y + 2.0f));
        if (ImGui::InvisibleButton("vis_dot", ImVec2(14.0f, 16.0f))) {
            t.hidden = !t.hidden;
        }
        const ImVec2 dotC(rMin.x + 8.0f, rMin.y + rH * 0.5f);
        if (!t.hidden) {
            dl->AddCircleFilled(dotC, 4.0f, C_BLUE);
        } else {
            dl->AddCircle(dotC, 4.0f, C_MUTED, 0, 1.2f);
        }

        // Track colour bar
        const ImU32 tCol = IM_COL32(t.color[0], t.color[1], t.color[2], 255);
        dl->AddRectFilled(ImVec2(rMin.x + 17.0f, rMin.y + 3.0f), ImVec2(rMin.x + 21.0f, rMin.y + rH - 3.0f), tCol, 1.0f);

        // Track name selectable
        ImGui::SetCursorScreenPos(ImVec2(rMin.x + 25.0f, rMin.y + 1.0f));
        if (ImGui::InvisibleButton("trk_sel_row", ImVec2(std::max(16.0f, rW - 26.0f), rH - 2.0f))) {
            if (!ImGui::GetIO().KeyShift) doc.sessionEdit.selectedTracks.clear();
            doc.sessionEdit.selectedTracks.push_back(t.numId);
            Editor::selectRange(Editor::selection().start, Editor::selection().end, t.id);
        }
        dl->PushClipRect(ImVec2(rMin.x + 25.0f, rMin.y), ImVec2(rMin.x + rW - 2.0f, rMin.y + rH), true);
        dl->AddText(ImVec2(rMin.x + 25.0f, rMin.y + 3.0f),
                    t.inactive ? C_MUTED : (isSel ? C_TEXT : C_DIM),
                    t.name.c_str());
        dl->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(rMin.x, rMin.y + rH + 1.0f));
        ImGui::PopID();
    }
    ImGui::EndChild();

    // GROUPS header + '+' button
    ImGui::Spacing();
    ImGui::TextColored(col4(C_DIM), "GROUPS");
    ImGui::SameLine(avail.x - 18.0f);
    if (Icons::iconButton("left_add_grp", ImVec2(18.0f, 16.0f), "plus", false, "Create Group (Ctrl+G)", C_BLUE)) {
        mutateDocument("Create Group", [](Session::Document& d) {
            Session::Group g;
            g.numId = (uint64_t)(d.groups.size() + 1);
            g.letter = (char)('a' + (d.groups.size() % 26));
            g.name = "Group " + std::to_string(g.numId);
            g.active = true;
            for (const auto& t : d.tracks) g.members.push_back(t.numId);
            d.groups.push_back(std::move(g));
        });
    }
    ImGui::Separator();

    ImGui::BeginChild("groups_list_scroll", ImVec2(0, 0), false);
    // Built-in <ALL> group
    {
        static bool sAllGroupActive = false;
        if (ImGui::Selectable("<ALL>", sAllGroupActive)) {
            sAllGroupActive = !sAllGroupActive;
        }
    }
    for (size_t gi = 0; gi < doc.groups.size(); ++gi) {
        auto& g = doc.groups[gi];
        ImGui::PushID((int)gi);
        const std::string grpLbl = std::string(1, g.letter) + "  " + g.name;
        if (ImGui::Selectable(grpLbl.c_str(), g.active)) {
            g.active = !g.active;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// ============================================================================
// Bottom Dock Tabs: MIDI EDITOR, MEMORY LOCATIONS, UNDO HISTORY & Status Bar
// ============================================================================

static void drawBottomDockContent(float height) {
    ImGui::BeginChild("sc_bottom_dock_body", ImVec2(0, height), true);
    if (gLowerTab == 0) {
        // MIDI Editor Piano Roll preview
        ImGui::TextColored(col4(C_DIM), "MIDI EDITOR");
        ImGui::SameLine();
        ImGui::TextDisabled("(Double-click a MIDI clip or select Notes view to edit notes)");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 sz = ImGui::GetContentRegionAvail();
        dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), C_BG0);
        const float keysW = 42.0f;
        const int numRows = 12;
        const float rowH = std::max(8.0f, sz.y / (float)numRows);
        for (int r = 0; r < numRows; ++r) {
            const int note = 71 - r;
            const int pc = note % 12;
            const bool black = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
            const float y0 = p0.y + r * rowH;
            dl->AddRectFilled(ImVec2(p0.x, y0), ImVec2(p0.x + keysW, y0 + rowH - 1.0f),
                              black ? IM_COL32(30, 30, 30, 255) : IM_COL32(215, 215, 215, 255));
            dl->AddRectFilled(ImVec2(p0.x + keysW, y0), ImVec2(p0.x + sz.x, y0 + rowH - 1.0f),
                              black ? IM_COL32(20, 20, 20, 255) : IM_COL32(26, 26, 26, 255));
        }
        // Render notes from the first MIDI clip in the session
        for (const auto& t : doc.tracks) {
            for (const auto& c : t.clips) {
                if (c.midi.has_value() && !c.midi->notes.empty()) {
                    for (const auto& n : c.midi->notes) {
                        const int row = std::clamp(71 - (int)n.pitch, 0, numRows - 1);
                        const float nx0 = p0.x + keysW + 8.0f + (float)n.start * 0.002f;
                        const float nx1 = nx0 + std::max(6.0f, (float)n.length * 0.002f);
                        const float ny0 = p0.y + row * rowH + 1.0f;
                        dl->AddRectFilled(ImVec2(nx0, ny0), ImVec2(nx1, ny0 + rowH - 2.0f), C_BLUE, 2.0f);
                    }
                    break;
                }
            }
        }
    } else if (gLowerTab == 1) {
        // Memory Locations table (after panels/memory.rs)
        if (ImGui::SmallButton("+ Add Marker")) {
            mutateDocument("Add Marker", [](Session::Document& d) {
                Session::Marker m;
                m.number = (unsigned)(d.markers.size() + 1);
                m.id = "m" + std::to_string(m.number);
                m.name = "Marker " + std::to_string(m.number);
                m.start = Playback::playhead();
                d.markers.push_back(std::move(m));
            });
        }
        ImGui::Separator();
        if (ImGui::BeginTable("mem_locs_tbl", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < doc.markers.size(); ++i) {
                const auto& m = doc.markers[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", m.number ? m.number : (unsigned)(i + 1));
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Selectable(m.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    Playback::setPlayhead(m.start);
                }
                ImGui::TableSetColumnIndex(2);
                const std::string posStr = TimeFormat::formatPosition(m.start, TimeFormat::Kind::BarsBeats, doc);
                ImGui::TextUnformatted(posStr.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(m.end > m.start ? "Selection" : "Marker");
            }
            ImGui::EndTable();
        }
    } else if (gLowerTab == 2) {
        // Undo History panel (after panels/history.rs)
        ImGui::BeginDisabled(!hist.canUndo());
        if (ImGui::SmallButton("Undo")) undoDocument();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!hist.canRedo());
        if (ImGui::SmallButton("Redo")) redoDocument();
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextColored(col4(C_TEXT), "Current State: %s (%zu tracks)", doc.name.c_str(), doc.tracks.size());
        if (!sessionNote.empty()) {
            ImGui::TextColored(col4(C_DIM), "Last action: %s", sessionNote.c_str());
        }
    }
    ImGui::EndChild();
}

// ============================================================================
// Preferences & About Dialogs
// ============================================================================

static void drawPrefs() {
    if (!showPrefs) return;
    ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(T("Preferences"), &showPrefs)) {
        if (ImGui::BeginTabBar("prefs")) {
            if (ImGui::BeginTabItem(T("General"))) {
                prefsPage = 0;
                const auto& packs = Lang::packs();
                if (ImGui::BeginCombo(T("Language"), packs[langIdx].name.c_str())) {
                    for (int i = 0; i < (int)packs.size(); ++i) {
                        if (ImGui::Selectable(packs[i].name.c_str(), i == langIdx)) {
                            langIdx = i;
                            Lang::load(langDir, packs[i].code);
                            savePrefs();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(T("Audio"))) {
                prefsPage = 1;
                const auto& devs = Audio::playbackDevices();
                const char* preview = devIdx >= 0 && devIdx < (int)devs.size() ? devs[devIdx].name.c_str() : T("(default)");
                if (ImGui::BeginCombo(T("Playback device"), preview)) {
                    if (ImGui::Selectable(T("(default)"), devIdx < 0)) devIdx = -1;
                    for (int i = 0; i < (int)devs.size(); ++i) {
                        if (ImGui::Selectable(devs[i].name.c_str(), i == devIdx)) devIdx = i;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SliderFloat(T("Buffer size (frames)"), &bufferSlider, 128, 2048, "%.0f");
                if (ImGui::Button(T("Apply"))) {
                    applyAudioDevice();
                    savePrefs();
                }
                if (!audioNote.empty()) {
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", audioNote.c_str());
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

static void drawAbout() {
    if (!showAbout) return;
    ImGui::SetNextWindowSize(ImVec2(Splash::W, Splash::H));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(T("About Polyhymnia"), &showAbout, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        Splash::draw(ImGui::GetWindowDrawList(), ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, nullptr, -1);
        if (ImGui::InvisibleButton("close", ImVec2(Splash::W, Splash::H))) showAbout = false;
    }
    ImGui::End();
}

void init(SDL_Window* win, const std::string& prefPath, const std::string& languageDir, SplashFn splash) {
    mainWin = win;
    prefDir = prefPath;
    langDir = languageDir;
    applyStyle();
    loadPrefs();
    Lang::load(langDir, Lang::packs()[langIdx].code);
    if (splash) splash(T("Opening the audio device..."), 0.7f);
    applyAudioDevice();
    if (splash) splash(T("Ready"), 1.0f);
}

// ============================================================================
// Full SoundCraft Menu Bar & Main Frame (after crates/ui-egui/src/menus.rs & edit_window.rs)
// ============================================================================

static void drawMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    // App brand menu ("Polyhymnia", like SoundCraft's bold app menu)
    if (ImGui::BeginMenu("Polyhymnia")) {
        if (ImGui::MenuItem(T("About Polyhymnia"))) showAbout = true;
        ImGui::Separator();
        if (ImGui::MenuItem(T("Preferences..."), shortcutLabel("preferences"))) showPrefs = true;
        ImGui::Separator();
        if (ImGui::MenuItem(T("Quit"), shortcutLabel("quit"))) quit = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("File"))) {
        if (ImGui::MenuItem(T("New Session..."), shortcutLabel("new"))) newDoc();
        if (ImGui::MenuItem(T("Open Demo Session"), nullptr)) openDemoDoc();
        if (ImGui::MenuItem(T("Save Session"), shortcutLabel("save"), false, hasDoc)) saveDoc();
        ImGui::Separator();
        if (ImGui::MenuItem(T("Import Audio..."), shortcutLabel("import"))) openImportAudioDialog();
        if (ImGui::BeginMenu(T("Import"))) {
            if (ImGui::MenuItem(T("Audio..."), shortcutLabel("import"))) openImportAudioDialog();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(T("Bounce Mix..."), shortcutLabel("bounce"), false, hasDoc)) {
            RecordBounce::openBounceDialog(doc);
        }
        if (ImGui::MenuItem(T("Bounce Stems..."), nullptr, false, hasDoc)) {
            const std::string stemsDir = (!doc.folder.empty() ? doc.folder : prefDir) + "/Bounced Stems";
            RecordBounce::EncodeOptions opts;
            opts.format = RecordBounce::EncodeFormat::Wav;
            opts.bitDepth = RecordBounce::BitDepth::Int24;
            Time::Range r = doc.sessionEdit.selection;
            if (r.isEmpty()) r = {0, std::max<long long>(1, Mix::sessionLength(doc))};
            auto [files, err] = RecordBounce::bounceStems(doc, stemsDir, r, opts, false);
            sessionNote = err.empty() ? ("Bounced " + std::to_string(files.size()) + " stem(s) to " + stemsDir) : err;
        }
        if (ImGui::BeginMenu(T("Export"), hasDoc)) {
            if (ImGui::MenuItem(T("Clips as Files..."), shortcutLabel("export_clips"))) {
                const std::string expDir = (!doc.folder.empty() ? doc.folder : prefDir) + "/Exported Clips";
                auto [n, err] = RecordBounce::exportClips(doc, doc.sessionEdit.selectedClips, expDir);
                sessionNote = err.empty() ? ("Exported " + std::to_string(n) + " clip(s) to " + expDir) : err;
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(T("Preferences..."))) showPrefs = true;
        ImGui::Separator();
        if (ImGui::MenuItem(T("Quit"), shortcutLabel("quit"))) quit = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("Edit"))) {
        const auto run = [&](const char* label, const char* shortcut, auto fn) {
            if (ImGui::MenuItem(label, shortcut, false, hasDoc)) {
                if (!hasDoc) newDoc();
                mutateDocument(label, [&](Session::Document& d) { fn(d); });
            }
        };
        if (ImGui::MenuItem(T("Undo"), shortcutLabel("undo"), false, hasDoc && hist.canUndo())) undoDocument();
        if (ImGui::MenuItem(T("Redo"), shortcutLabel("redo"), false, hasDoc && hist.canRedo())) redoDocument();
        ImGui::Separator();
        run("Cut", shortcutLabel("cut"), Editor::cut);
        if (ImGui::MenuItem(T("Copy"), shortcutLabel("copy"), false, hasDoc)) Editor::copy(doc);
        run("Paste", shortcutLabel("paste"), [](Session::Document& d) { Editor::paste(d, Playback::playhead(), Editor::selection().trackId); });
        run("Clear", shortcutLabel("clear"), [](Session::Document& d) {
            EngCmd::Engine e;
            e.doc = std::move(d);
            e.execute("edit.clear", {});
            d = std::move(e.doc);
        });
        run("Duplicate", shortcutLabel("duplicate"), Editor::duplicateSelection);
        ImGui::Separator();
        run("Separate Clip at Selection", shortcutLabel("separate"), [](Session::Document& d) { Editor::separateAtPlayhead(d, Playback::playhead()); });
        run("Heal Separation", shortcutLabel("heal"), Editor::healSeparation);
        run("Consolidate Clip", "Alt+Shift+3", Editor::consolidateSelection);
        ImGui::Separator();
        if (ImGui::BeginMenu(T("Fades"))) {
            if (ImGui::MenuItem(T("Create..."), shortcutLabel("fades"), false, hasDoc)) Edit::openFadesDialog();
            run("Fade to Start", shortcutLabel("fade_start"), [](Session::Document& d) {
                EngCmd::Engine e;
                e.doc = std::move(d);
                e.execute("edit.fade_to_start", {});
                d = std::move(e.doc);
            });
            run("Fade to End", shortcutLabel("fade_end"), [](Session::Document& d) {
                EngCmd::Engine e;
                e.doc = std::move(d);
                e.execute("edit.fade_to_end", {});
                d = std::move(e.doc);
            });
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("View"))) {
        if (ImGui::MenuItem(T("Narrow Mix"), "Ctrl+Alt+M", MixWindow::isNarrowMix())) {
            MixWindow::toggleNarrowMix();
        }
        ImGui::Separator();
        ImGui::MenuItem(T("Tracks & Groups List"), nullptr, &gShowLeftDock);
        ImGui::MenuItem(T("Clips List"), nullptr, &gShowRightDock);
        ImGui::Separator();
        if (ImGui::MenuItem(T("MIDI Editor"), nullptr, gLowerTab == 0)) gLowerTab = (gLowerTab == 0) ? -1 : 0;
        if (ImGui::MenuItem(T("Memory Locations"), nullptr, gLowerTab == 1)) gLowerTab = (gLowerTab == 1) ? -1 : 1;
        if (ImGui::MenuItem(T("Undo History"), nullptr, gLowerTab == 2)) gLowerTab = (gLowerTab == 2) ? -1 : 2;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("Track"))) {
        if (ImGui::MenuItem(T("New..."), "Ctrl+Shift+N")) openNewTracksDialog();
        if (ImGui::MenuItem(T("Duplicate"), "Alt+Shift+D", false, hasDoc && !doc.tracks.empty())) {
            const uint64_t tid = !doc.sessionEdit.selectedTracks.empty() ? doc.sessionEdit.selectedTracks.front() : doc.tracks.front().numId;
            mutateDocument("Duplicate Track", [tid](Session::Document& d) {
                EngCmd::Engine e;
                e.doc = std::move(d);
                EngCmd::Engine::Params p;
                p.nums["track"] = (double)tid;
                e.execute("track.duplicate", p);
                d = std::move(e.doc);
            });
        }
        if (ImGui::MenuItem(T("Group..."), "Ctrl+G", false, hasDoc)) {
            mutateDocument("Create Group", [](Session::Document& d) {
                Session::Group g;
                g.numId = (uint64_t)(d.groups.size() + 1);
                g.letter = (char)('a' + (d.groups.size() % 26));
                g.name = "Group " + std::to_string(g.numId);
                for (const auto& t : d.tracks) g.members.push_back(t.numId);
                d.groups.push_back(std::move(g));
            });
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("Clip"))) {
        if (ImGui::MenuItem(T("Mute/Unmute Clips"), "Ctrl+M", false, hasDoc)) {
            mutateDocument("Toggle Clip Mute", [](Session::Document& d) {
                EngCmd::Engine e;
                e.doc = std::move(d);
                e.execute("clip.mute_toggle", {});
                d = std::move(e.doc);
            });
        }
        if (ImGui::MenuItem(T("Lock/Unlock Clip"), nullptr, false, hasDoc)) {
            mutateDocument("Toggle Clip Lock", [](Session::Document& d) {
                EngCmd::Engine e;
                e.doc = std::move(d);
                e.execute("clip.lock_toggle", {});
                d = std::move(e.doc);
            });
        }
        if (ImGui::MenuItem(T("Spot..."), nullptr, false, hasDoc)) {
            Edit::openSpotForScript(doc);
        }
        if (ImGui::MenuItem(T("Fades..."), "Ctrl+F", false, hasDoc)) {
            Edit::openFadesDialog();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("Event"))) {
        if (ImGui::MenuItem(T("Add Marker at Playhead"), "Enter", false, hasDoc)) {
            mutateDocument("Add Marker", [](Session::Document& d) {
                Session::Marker m;
                m.number = (unsigned)(d.markers.size() + 1);
                m.id = "m" + std::to_string(m.number);
                m.name = "Marker " + std::to_string(m.number);
                m.start = Playback::playhead();
                d.markers.push_back(std::move(m));
            });
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("AudioSuite"))) {
        const auto runSuite = [&](const char* label, const char* cmd, EngCmd::Engine::Params p = {}) {
            if (ImGui::MenuItem(label, nullptr, false, hasDoc)) {
                mutateDocument(label, [=](Session::Document& d) {
                    EngCmd::Engine e;
                    e.doc = std::move(d);
                    e.execute(cmd, p);
                    d = std::move(e.doc);
                });
            }
        };
        runSuite("Gain (+3 dB)", "audiosuite.gain", [] { EngCmd::Engine::Params p; p.nums["db"] = 3.0; return p; }());
        runSuite("Normalize", "audiosuite.normalize");
        runSuite("Reverse", "audiosuite.reverse");
        runSuite("Invert Phase", "audiosuite.invert_phase");
        runSuite("DC Offset Removal", "audiosuite.dc_offset");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("Options"))) {
        if (ImGui::MenuItem(T("Loop Playback"), shortcutLabel("loop"), Playback::loopEnabled())) Playback::toggleLoop();
        if (hasDoc) {
            ImGui::MenuItem(T("Metronome Click"), shortcutLabel("click"), &doc.click);
            ImGui::MenuItem(T("Count Off"), nullptr, &doc.countoff);
            ImGui::Separator();
            auto menuFlag = [&](const char* label, const char* key, const char* sc = nullptr, bool def = false) {
                bool val = doc.sessionEdit.flags.count(key) ? doc.sessionEdit.flags[key] : def;
                if (ImGui::MenuItem(label, sc, &val)) {
                    doc.sessionEdit.flags[key] = val;
                }
            };
            menuFlag(T("Tab to Transients"), "options.tab_to_transient");
            menuFlag(T("Link Timeline and Edit Selection"), "options.link_timeline_edit", nullptr, true);
            menuFlag(T("Link Track and Edit Selection"), "options.link_track_edit");
            menuFlag(T("Insertion Follows Playback"), "options.insertion_follows_playback", "N");
            menuFlag(T("Mirrored MIDI Editing"), "options.mirrored_midi");
            ImGui::MenuItem(T("Automation Follows Edit"), nullptr, &doc.edit.automationFollowsEdit);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("Setup"))) {
        if (ImGui::MenuItem(T("Audio Engine /Playback Device..."))) {
            showPrefs = true;
            prefsPage = 1;
        }
        if (ImGui::MenuItem(T("Preferences..."))) {
            showPrefs = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("Window"))) {
        if (ImGui::MenuItem(T("Edit"), "Ctrl+=", gMainWindow == 0)) gMainWindow = 0;
        if (ImGui::MenuItem(T("Mix"), "Ctrl+=", gMainWindow == 1)) gMainWindow = 1;
        ImGui::Separator();
        if (ImGui::MenuItem(T("Memory Locations"), nullptr, gLowerTab == 1)) gLowerTab = (gLowerTab == 1) ? -1 : 1;
        if (ImGui::MenuItem(T("Undo History"), nullptr, gLowerTab == 2)) gLowerTab = (gLowerTab == 2) ? -1 : 2;
        ImGui::Separator();
        if (ImGui::MenuItem(T("About Polyhymnia"))) showAbout = true;
        ImGui::EndMenu();
    }

    // Right-aligned "<Session Name> — Edit/Mix" label on menu bar (after menus.rs)
    if (hasDoc) {
        const std::string rightTitle = doc.name + (gMainWindow == 1 ? "  \xe2\x80\x94  Mix" : " \xe2\x80\x94 Edit");
        const float tw = ImGui::CalcTextSize(rightTitle.c_str()).x;
        const float rx = ImGui::GetWindowWidth() - tw - 14.0f;
        if (rx > ImGui::GetCursorPosX() + 16.0f) {
            ImGui::SameLine(rx);
            ImGui::TextColored(col4(C_DIM), "%s", rightTitle.c_str());
        }
    }

    ImGui::EndMainMenuBar();
}

// after SoundCraft (crates/ui-egui/src/shortcuts.rs): dispatch before any menu or widget gets the key.
static void handleGlobalShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
    // Dispatch the same chords shown in menus even when no menu is open.
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false) && !shift) { newDoc(); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false)) { showPrefs = true; return; }
    if (alt && ImGui::IsKeyPressed(ImGuiKey_F4, false)) { quit = true; return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_G, false) && hasDoc) {
        mutateDocument("Group", [](Session::Document& d) {
            Session::Group g;
            g.numId = static_cast<uint64_t>(d.groups.size() + 1);
            g.letter = static_cast<char>('a' + (d.groups.size() % 26));
            g.name = std::string("Group ") + g.letter;
            d.groups.push_back(std::move(g));
        });
        return;
    }
    if (alt && shift && ImGui::IsKeyPressed(ImGuiKey_D, false) && hasDoc) {
        mutateDocument("Duplicate Track", [](Session::Document& d) {
            if (d.sessionEdit.selectedTracks.empty()) return;
            const uint64_t id = d.sessionEdit.selectedTracks.front();
            auto it = std::find_if(d.tracks.begin(), d.tracks.end(), [id](const Session::Track& t) { return t.numId == id; });
            if (it == d.tracks.end()) return;
            Session::Track copy = *it;
            copy.numId = Session::allocId(d);
            copy.id = "t" + std::to_string(copy.numId);
            copy.name += " Copy";
            d.tracks.insert(std::next(it), std::move(copy));
        });
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ctrl && !alt) {
        if (Playback::isPlaying()) Playback::stop(); else Playback::play();
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Equal, false)) { gMainWindow = gMainWindow == 0 ? 1 : 0; return; }
    if (alt && ImGui::IsKeyPressed(ImGuiKey_F4, false)) { quit = true; return; }
    if (ctrl && alt && ImGui::IsKeyPressed(ImGuiKey_B, false) && hasDoc) { RecordBounce::openBounceDialog(doc); return; }
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false) && hasDoc) { RecordBounce::toggleRecording(doc, sessionNote); return; }
    if (!hasDoc) {
        if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_I, false)) openImportAudioDialog();
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) newDoc();
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (shift) redoDocument(); else undoDocument(); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) { mutateDocument("Duplicate", [](Session::Document& d) { Editor::duplicateSelection(d); }); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) { mutateDocument("Separate", [](Session::Document& d) { Editor::separateAtPlayhead(d, Playback::playhead()); }); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) { mutateDocument("Cut", Editor::cut); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) { Editor::copy(doc); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) { mutateDocument("Paste", [](Session::Document& d) { Editor::paste(d, Playback::playhead(), Editor::selection().trackId); }); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_H, false)) { mutateDocument("Heal Separation", Editor::healSeparation); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_M, false) && !alt) { mutateDocument("Mute Clips", [](Session::Document& d) { EngCmd::Engine e; e.doc = std::move(d); e.execute("clip.mute_toggle", {}); d = std::move(e.doc); }); return; }
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_L, false)) { Playback::toggleLoop(); return; }
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_I, false)) { openImportAudioDialog(); return; }
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_N, false)) { openNewTracksDialog(); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) { saveDoc(); return; }
    if (ctrl && alt && ImGui::IsKeyPressed(ImGuiKey_M, false)) { MixWindow::toggleNarrowMix(); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) { Edit::openFadesDialog(); return; }
    if (alt && ImGui::IsKeyPressed(ImGuiKey_D, false)) { mutateDocument("Fade to Start", [](Session::Document& d) { EngCmd::Engine e; e.doc = std::move(d); e.execute("edit.fade_to_start", {}); d = std::move(e.doc); }); return; }
    if (alt && ImGui::IsKeyPressed(ImGuiKey_G, false)) { mutateDocument("Fade to End", [](Session::Document& d) { EngCmd::Engine e; e.doc = std::move(d); e.execute("edit.fade_to_end", {}); d = std::move(e.doc); }); return; }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ctrl && !alt && !shift) {
        mutateDocument("Add Marker", [](Session::Document& d) { Session::Marker m; m.number = static_cast<unsigned>(d.markers.size() + 1); m.id = "m" + std::to_string(m.number); m.name = "Marker " + std::to_string(m.number); m.start = Playback::playhead(); d.markers.push_back(std::move(m)); }); return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ctrl && !alt) { mutateDocument("Clear", [](Session::Document& d) { EngCmd::Engine e; e.doc = std::move(d); e.execute("edit.clear", {}); d = std::move(e.doc); }); return; }
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false) || (ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false))) RecordBounce::toggleRecording(doc, sessionNote);
}

void frame() {
    handleGlobalShortcuts();
    const ImGuiIO& io = ImGui::GetIO();
    if (hasDoc) {
        RecordBounce::tickRecording(doc, sessionNote);
        RecordBounce::stepBounceUi(sessionNote);
        Playback::setDocument(&doc);
        const auto plan = Mix::currentPlan();
        if (plan && !plan->missingNote.empty() && sessionNote.empty()) sessionNote = plan->missingNote;
    }

    drawMainMenuBar();

    const float menuH = ImGui::GetFrameHeight();
    const float statusBarH = (gMainWindow == 1) ? 0.0f : 24.0f;
    const float winW = io.DisplaySize.x;
    const float winH = io.DisplaySize.y - menuH - statusBarH;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowPos(ImVec2(0, menuH));
    ImGui::SetNextWindowSize(ImVec2(winW, winH));
    ImGui::Begin("workspace", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (hasDoc && gMainWindow == 1) {
        // Mix Window layout: Left TRACKS & GROUPS dock + Channel Strips (matching SoundCraft mix.png)
        const float leftW = gShowLeftDock ? 168.0f : 0.0f;
        const float centerW = std::max(160.0f, winW - leftW);
        if (gShowLeftDock) {
            drawLeftTracksAndGroupsDock(leftW);
            ImGui::SameLine(0.0f, 0.0f);
        }
        ImGui::BeginChild("mix_center_col", ImVec2(centerW, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        MixWindow::draw(doc);
        ImGui::EndChild();
    } else if (hasDoc) {
        // 1. Top Toolbar across full width
        Edit::drawToolbar(doc);

        // 2. Main middle region (Left Dock | Timeline | Right Dock) + Bottom Dock
        const float lowerPanelTotalH = (gLowerTab >= 0) ? gLowerDockH : 0.0f;
        const float midH = std::max(80.0f, ImGui::GetContentRegionAvail().y - lowerPanelTotalH);

        ImGui::BeginChild("edit_middle_row", ImVec2(0, midH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const float leftW = gShowLeftDock ? gLeftDockW : 0.0f;
        const float rightW = gShowRightDock ? gRightDockW : 0.0f;
        const float centerW = std::max(160.0f, winW - leftW - rightW);

        if (gShowLeftDock) {
            drawLeftTracksAndGroupsDock(leftW);
            ImGui::SameLine(0.0f, 0.0f);
        }

        ImGui::BeginChild("edit_center_col", ImVec2(centerW, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        Edit::draw(doc, 0.0f);
        ImGui::EndChild();

        if (gShowRightDock) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ClipsPanel::draw(doc, rightW);
            ImGui::PopStyleVar(2);
        }
        ImGui::EndChild();

        // 3. Lower Dock Content (when a bottom tab is active)
        if (gLowerTab >= 0) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            drawBottomDockContent(lowerPanelTotalH);
            ImGui::PopStyleVar(2);
        }
    } else {
        // Empty workspace prompt with quick buttons
        ImGui::SetCursorPos(ImVec2(28.0f, 28.0f));
        ImGui::BeginGroup();
        ImGui::TextColored(col4(C_TEXT), "%s", T("No project open yet."));
        ImGui::Spacing();
        ImGui::TextDisabled("%s", T("File > New Session, File > Open Demo Session, or File > Import Audio (Ctrl+Shift+I) to begin."));
        ImGui::Spacing();
        if (ImGui::Button(T("Open Midnight Groove Demo"), ImVec2(220, 28))) openDemoDoc();
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(T("New Blank Session"), ImVec2(180, 28))) newDoc();
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(T("Import Audio..."), ImVec2(160, 28))) openImportAudioDialog();
        ImGui::EndGroup();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);

    // Bottom Status & Tab Bar (in Edit window)
    if (statusBarH > 0.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
        ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - statusBarH));
        ImGui::SetNextWindowSize(ImVec2(winW, statusBarH));
        ImGui::Begin("sc_status_bar", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
        ImDrawList* sdl = ImGui::GetWindowDrawList();
        sdl->AddLine(ImVec2(0, io.DisplaySize.y - statusBarH), ImVec2(winW, io.DisplaySize.y - statusBarH), C_BORDER);

        auto tabBtn = [&](const char* label, int idx) {
            const bool active = (gLowerTab == idx);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_BLUE));
            if (ImGui::SmallButton(label)) {
                gLowerTab = active ? -1 : idx;
            }
            if (active) ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        tabBtn("MIDI EDITOR", 0);
        tabBtn("MEMORY LOCATIONS", 1);
        tabBtn("UNDO HISTORY", 2);

        if (!sessionNote.empty()) {
            ImGui::SameLine(0.0f, 12.0f);
            ImGui::TextColored(col4(C_DIM), "%s", sessionNote.c_str());
        }

        if (hasDoc) {
            char rightStat[256];
            std::snprintf(rightStat, sizeof(rightStat), "%s \xc2\xb7 %.0f Hz \xc2\xb7 %d-bit \xc2\xb7 %zu tracks",
                          doc.name.c_str(), doc.sampleRate, doc.bitDepth, doc.tracks.size());
            const float rw = ImGui::CalcTextSize(rightStat).x;
            const float rx = winW - rw - 12.0f;
            if (rx > ImGui::GetCursorPosX() + 12.0f) {
                ImGui::SameLine(rx);
                ImGui::TextColored(col4(C_DIM), "%s", rightStat);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    // Floating dialogs & modals
    drawPrefs();
    drawAbout();
    drawImportAudioDialog();
    drawNewTracksDialog();
    if (hasDoc) {
        Edit::drawModals(doc);
        Spot::draw(doc);
        ClipsPanel::drawDialogs(doc);
        MixWindow::drawFloatingWindows(doc);
        RecordBounce::drawBounceUi(doc, sessionNote);
    }
    gImports.show();
}

void script(const std::string& name) {
    if (name == "skeleton") {
        showPrefs = true;
        prefsPage = 1;
        fprintf(stderr, "skeleton: window up, audio device %s, preferences on Audio\n", Audio::isOpen() ? "open" : "closed");
        return;
    }
    if (name == "session") {
        doc = Session::demo();
        hasDoc = true;
        const std::string path = prefDir + "session-test.polyhymnia";
        std::string err;
        if (!Project::save(doc, path, err)) {
            fprintf(stderr, "session: save failed: %s\n", err.c_str());
            return;
        }
        Session::Document round;
        if (!Project::load(path, round, err)) {
            fprintf(stderr, "session: load failed: %s\n", err.c_str());
            return;
        }
        doc = round;
        fprintf(stderr, "session: %zu tracks, %zu markers, saved and reopened %s\n", doc.tracks.size(), doc.markers.size(), path.c_str());
        return;
    }
    if (name == "playback") {
        doc = Session::demo();
        hasDoc = true;
        Playback::setDocument(&doc);
        Playback::toggleLoop();
        Playback::play();
        fprintf(stderr, "playback: playing demo, loop %s, %zu tracks\n", Playback::loopEnabled() ? "on" : "off", doc.tracks.size());
        return;
    }
    if (name == "shortcuts") {
        doc = Session::demo();
        hasDoc = true;
        hist.clear();
        Playback::setDocument(&doc);
        if (!doc.tracks.empty() && !doc.tracks.front().clips.empty()) {
            const auto& tr = doc.tracks.front();
            Editor::selectClip(doc, tr.id, tr.clips.front().id);
        }
        Playback::stop();
        Playback::play();
        const bool started = Playback::isPlaying();
        Playback::stop();
        const size_t beforeDuplicate = doc.tracks.empty() ? 0 : doc.tracks.front().clips.size();
        if (hasDoc) mutateDocument("Shortcut duplicate", [](Session::Document& d) { Editor::duplicateSelection(d); });
        const size_t afterDuplicate = doc.tracks.empty() ? 0 : doc.tracks.front().clips.size();
        if (!doc.tracks.empty() && !doc.tracks.front().clips.empty()) {
            const auto& clip = doc.tracks.front().clips.front();
            Playback::setPlayhead(clip.start + clip.length / 2);
        }
        if (hasDoc) mutateDocument("Shortcut separate", [](Session::Document& d) { Editor::separateAtPlayhead(d, Playback::playhead()); });
        const size_t afterSeparate = doc.tracks.empty() ? 0 : doc.tracks.front().clips.size();
        const bool undoWorked = undoDocument();
        const size_t afterUndo = doc.tracks.empty() ? 0 : doc.tracks.front().clips.size();
        fprintf(stderr, "shortcuts: Space %s, Ctrl+D %s, Ctrl+E %s, Ctrl+Z %s\n",
                started && !Playback::isPlaying() ? "play/stop ok" : "FAILED",
                afterDuplicate > beforeDuplicate ? "duplicate ok" : "FAILED",
                afterSeparate > afterDuplicate ? "separate ok" : "FAILED",
                undoWorked && afterUndo == afterDuplicate ? "undo ok" : "FAILED");
        return;
    }
    if (name == "editing") {
        doc = Session::demo();
        doc.folder = prefDir;
        std::string err;
        const std::string adir = prefDir + "audio";
        std::filesystem::create_directories(adir);
        const int rate = (int)doc.sampleRate;
        Mix::writeTestToneWav(adir + "/sine_440.wav", 440.0, 2.0, rate, err);
        Mix::writeTestToneWav(adir + "/click.wav", 2200.0, 0.08, rate, err);
        hasDoc = true;
        hist.clear();
        Editor::setMode(Editor::Mode::Grid);
        Editor::setTool(Editor::Tool::Smart);
        mutateDocument("Editing demo", [](Session::Document& d) { Editor::scriptDemo(d); });
        Edit::scriptSetup(doc);
        Playback::setDocument(&doc);
        fprintf(stderr, "editing: modes/tools, separate, fades, clip gain, duplicate (%zu clips on track 1)\n",
                doc.tracks.empty() ? 0 : doc.tracks[0].clips.size());
        return;
    }
    if (name == "spot_test") {
        doc = Session::demo();
        doc.folder = prefDir;
        hasDoc = true;
        hist.clear();
        Edit::scriptSetup(doc);
        Edit::openSpotForScript(doc);
        Playback::setDocument(&doc);
        fprintf(stderr, "spot_test: Spot dialog with start/sync/end fields open\n");
        return;
    }
    if (name == "edit") {
        gMainWindow = 0;
        MixWindow::closeAllPluginWindows();
        doc = Session::midnightGrooveDemo();
        doc.folder = prefDir;
        hasDoc = true;
        hist.clear();
        Edit::scriptSetup(doc);
        Playback::setDocument(&doc);
        fprintf(stderr, "edit: Midnight Groove demo session, %zu tracks, %zu sources, %zu markers\n",
                doc.tracks.size(), doc.sources.size(), doc.markers.size());
        return;
    }
    if (name == "mix") {
        gMainWindow = 1;
        MixWindow::closeAllPluginWindows();
        doc = Session::midnightGrooveDemo();
        doc.folder = prefDir;
        hasDoc = true;
        hist.clear();
        // Match SoundCraft mix.png: Bass + Pad selected, Kick's EQ 7-Band window open on the right.
        if (doc.tracks.size() >= 5) {
            doc.sessionEdit.selectedTracks = {doc.tracks[3].numId, doc.tracks[4].numId};
        }
        if (!doc.tracks.empty()) {
            MixWindow::openPluginWindow(doc.tracks[0].numId, 0);
        }
        Playback::setDocument(&doc);
        fprintf(stderr, "mix: Midnight Groove mixer, %zu channel strips, %zu buses, Kick EQ 7-Band open\n",
                doc.tracks.size(), doc.busses.size());
        return;
    }
    if (name == "fades_test") {
        doc = Session::midnightGrooveDemo();
        doc.folder = prefDir;
        hasDoc = true;
        hist.clear();
        Edit::scriptSetup(doc);
        Edit::openFadesDialog();
        Playback::setDocument(&doc);
        fprintf(stderr, "fades_test: Fades dialog open on Midnight Groove (%zu tracks)\n", doc.tracks.size());
        return;
    }
    if (name == "import") {
        newDoc();
        doc.name = "Import & Drop Session";
        doc.folder = prefDir + "import_project";
        std::filesystem::create_directories(doc.folder);
        const std::string srcDir = prefDir + "import_src";
        std::filesystem::create_directories(srcDir);
        const uint32_t rate = static_cast<uint32_t>(doc.sampleRate);

        auto makeToneBuf = [&](double freq, double durSec, int chans) {
            Session::AudioBuffer buf;
            buf.sampleRate = rate;
            const size_t n = static_cast<size_t>(rate * durSec);
            buf.channels.assign(static_cast<size_t>(chans), std::vector<float>(n, 0.0f));
            for (size_t i = 0; i < n; ++i) {
                const float beatPhase = std::fmod(static_cast<float>(i) / static_cast<float>(rate / 4), 1.0f);
                const float env = 0.25f + 0.75f * std::exp(-beatPhase * 4.5f);
                const float s = 0.85f * env * std::sin(2.0f * 3.14159265f * static_cast<float>(freq) * static_cast<float>(i) / static_cast<float>(rate));
                for (int c = 0; c < chans; ++c) {
                    buf.channels[static_cast<size_t>(c)][i] = s;
                }
            }
            return buf;
        };

        const std::string wavPath = srcDir + "/Drums_Loop.wav";
        const std::string aiffPath = srcDir + "/Bass_Groove.aiff";
        const std::string flacPath = srcDir + "/Synth_Chord.flac";
        const std::string clipListPath = srcDir + "/Vocal_Hook.wav";

        {
            const auto b = ImportAudio::encodeWav(makeToneBuf(220.0, 1.5, 2), 24);
            std::ofstream f(wavPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        {
            const auto b = ImportAudio::encodeAiff(makeToneBuf(110.0, 1.2, 1), 16);
            std::ofstream f(aiffPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        {
            const auto b = ImportAudio::encodeFlac(makeToneBuf(330.0, 1.4, 2));
            std::ofstream f(flacPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        {
            const auto b = ImportAudio::encodeWav(makeToneBuf(440.0, 1.0, 1), 24);
            std::ofstream f(clipListPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }

        runImportPaths({wavPath}, 0, 0, true);
        const uint64_t track1Num = doc.tracks.empty() ? 0 : doc.tracks[0].numId;
        runImportPaths({aiffPath}, 1, 0, true, track1Num, 84000);
        runImportPaths({flacPath}, 0, 0, true, std::nullopt, 24000);
        runImportPaths({clipListPath}, 2, 0, true);

        Edit::scriptSetup(doc);
        Playback::setDocument(&doc);
        fprintf(stderr, "import: %zu sources in Clips list, %zu tracks, Audio Files copied\n",
                doc.sources.size(), doc.tracks.size());
        return;
    }
    if (name == "record") {
        gMainWindow = 0;
        MixWindow::closeAllPluginWindows();
        doc = Session::midnightGrooveDemo();
        doc.folder = prefDir + "record_project";
        std::filesystem::create_directories(doc.folder);
        hasDoc = true;
        hist.clear();
        // Add a dedicated Vocal take track at the top, arm it + input monitor it, and start recording at bar 3!
        Session::Track vox;
        vox.id = "t_vox";
        vox.numId = Session::allocId(doc);
        vox.name = "Vocal";
        vox.kind = Session::TrackKind::Audio;
        vox.format = Session::ChannelFormat::Mono;
        vox.color = {230, 68, 68};
        vox.recordArm = true;
        vox.inputMonitor = true;
        vox.mixer.input = Session::Route::hardware("In 1");
        doc.tracks.insert(doc.tracks.begin(), vox);
        doc.sessionEdit.selectedTracks = {vox.numId};
        doc.sessionEdit.selection = {96000, 288000};
        Edit::scriptSetup(doc);
        Playback::setDocument(&doc);
        RecordBounce::toggleRecording(doc, sessionNote);
        for (int i = 0; i < 16; ++i) {
            RecordBounce::tickRecording(doc, sessionNote);
        }
        Playback::setPlayhead(216000);
        fprintf(stderr, "record: armed Vocal track (In 1), recording live take at bar 2..4 (%s)\n", sessionNote.c_str());
        return;
    }
    if (name == "bounce") {
        gMainWindow = 0;
        MixWindow::closeAllPluginWindows();
        doc = Session::midnightGrooveDemo();
        doc.folder = prefDir + "bounce_project";
        std::filesystem::create_directories(doc.folder);
        hasDoc = true;
        hist.clear();
        Edit::scriptSetup(doc);
        Playback::setDocument(&doc);
        RecordBounce::openBounceDialog(doc);
        fprintf(stderr, "bounce: Bounce Mix dialog open on Midnight Groove (%zu tracks)\n", doc.tracks.size());
        return;
    }
}

const Session::Document& document() { return doc; }
bool hasSession() { return hasDoc; }

void setCaptureHints(bool prefs, int page) {
    showPrefs = prefs;
    prefsPage = page;
}

bool wantsQuit() { return quit; }
void requestQuit() { quit = true; }

bool mutateDocument(const std::string& label, const std::function<void(Session::Document&)>& fn) {
    if (!hasDoc) return false;
    hist.pushBefore(doc, label);
    fn(doc);
    ++doc.touch;
    Playback::setDocument(&doc);
    return true;
}

bool undoDocument() {
    if (!hasDoc || !hist.doUndo(doc)) return false;
    ++doc.touch;
    Playback::setDocument(&doc);
    return true;
}

bool redoDocument() {
    if (!hasDoc || !hist.doRedo(doc)) return false;
    ++doc.touch;
    Playback::setDocument(&doc);
    return true;
}

} // namespace App
