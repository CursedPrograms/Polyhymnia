// Polyhymnia - Edit window shell, preferences (language and audio device), test hooks.
#include "app.h"
#include "audio.h"
#include "clips_panel.h"
#include "lang.h"
#include "edit.h"
#include "spot_dialog.h"
#include "editor.h"
#include "engine.h"
#include "import_audio.h"
#include "mix.h"
#include "undo.h"
#include "playback.h"
#include "project.h"
#include "splash.h"

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
static EngCmd::Engine gImportEngine;
static ImportAudio::Imports gImports;

struct ImportAudioDialogState {
    bool open = false;
    char pathBuf[1024] = "";
    std::vector<std::string> files;
    int destMode = 0; // 0 = New Track, 1 = Selected Track, 2 = Clip List Only
    int locMode = 0;  // 0 = Session Start, 1 = Selection / Playhead, 2 = Spot
    bool copyToAudioFiles = true;
};
static ImportAudioDialogState gImportDlg;

static const ImU32 C_BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), C_PANEL = IM_COL32(0x21, 0x21, 0x21, 255), C_WELL = IM_COL32(0x2a, 0x2a, 0x2a, 255),
                   C_LINE = IM_COL32(0x33, 0x33, 0x33, 255), C_TEXT = IM_COL32(0xd6, 0xd6, 0xd6, 255), C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   C_BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255), C_DEEP = IM_COL32(0x00, 0x5c, 0x8a, 255);

static ImVec4 col4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// Atelier greys and blue everywhere (after Mosaic Mask gui.cpp applyStyle); no default ImGui navy.
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
    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(7, 4);
    s.ItemSpacing = ImVec2(6, 7);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col4(C_TEXT);
    c[ImGuiCol_TextDisabled] = col4(C_DIM);
    c[ImGuiCol_WindowBg] = col4(C_PANEL);
    c[ImGuiCol_ChildBg] = col4(C_PANEL);
    c[ImGuiCol_PopupBg] = col4(C_PANEL);
    c[ImGuiCol_Border] = col4(C_LINE);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Separator] = col4(C_LINE);
    c[ImGuiCol_SeparatorHovered] = col4(C_DEEP);
    c[ImGuiCol_SeparatorActive] = col4(C_BLUE);
    c[ImGuiCol_FrameBg] = col4(C_WELL);
    c[ImGuiCol_FrameBgHovered] = col4(IM_COL32(0x2e, 0x2e, 0x2e, 255));
    c[ImGuiCol_FrameBgActive] = col4(IM_COL32(0x33, 0x33, 0x33, 255));
    c[ImGuiCol_TitleBg] = col4(C_BG);
    c[ImGuiCol_TitleBgActive] = col4(C_BG);
    c[ImGuiCol_TitleBgCollapsed] = col4(C_BG);
    c[ImGuiCol_MenuBarBg] = col4(C_BG);
    c[ImGuiCol_ScrollbarBg] = col4(C_PANEL);
    c[ImGuiCol_ScrollbarGrab] = col4(C_LINE);
    c[ImGuiCol_ScrollbarGrabHovered] = col4(C_DEEP);
    c[ImGuiCol_ScrollbarGrabActive] = col4(C_BLUE);
    c[ImGuiCol_CheckMark] = col4(C_BLUE);
    c[ImGuiCol_SliderGrab] = col4(C_BLUE);
    c[ImGuiCol_SliderGrabActive] = col4(C_DEEP);
    c[ImGuiCol_Button] = col4(IM_COL32(0x33, 0x33, 0x33, 255));
    c[ImGuiCol_ButtonHovered] = col4(C_DEEP);
    c[ImGuiCol_ButtonActive] = col4(C_BLUE);
    c[ImGuiCol_Header] = col4(IM_COL32(0x33, 0x33, 0x33, 255));
    c[ImGuiCol_HeaderHovered] = col4(C_DEEP);
    c[ImGuiCol_HeaderActive] = col4(C_BLUE);
    c[ImGuiCol_Tab] = col4(C_BG);
    c[ImGuiCol_TabHovered] = col4(C_DEEP);
    c[ImGuiCol_TabActive] = col4(C_PANEL);
    c[ImGuiCol_TabUnfocused] = col4(C_BG);
    c[ImGuiCol_TabUnfocusedActive] = col4(C_PANEL);
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
    const auto& devs = Audio::playbackDevices();
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
        // Dropped onto the Clips list on the right -> import into Clip List.
        runImportPaths({path}, 2, 0, true);
        return;
    }

    const Edit::DropTarget hit = Edit::hitTestDrop(doc, x, y);
    if (hit.inTimeline && hit.trackNumId != 0) {
        // Dropped onto a track lane -> place on that track where dropped (or open Spot in Spot mode).
        const int locMode = (Editor::mode() == Editor::Mode::Spot) ? 2 : 0;
        runImportPaths({path}, 1, locMode, true, hit.trackNumId, hit.sampleAt);
    } else if (hit.inTimeline) {
        // Dropped onto empty timeline space below tracks -> create a new track at the drop position.
        const int locMode = (Editor::mode() == Editor::Mode::Spot) ? 2 : 0;
        runImportPaths({path}, 0, locMode, true, std::nullopt, hit.sampleAt);
    } else {
        // Dropped elsewhere on the window -> create a new track at playhead/selection.
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

static void saveDoc() {
    if (!hasDoc) return;
    if (docPath.empty()) docPath = (std::filesystem::u8path(prefDir) / "Untitled.polyhymnia").u8string();
    std::string err;
    if (Project::save(doc, docPath, err)) sessionNote = T("Saved");
    else sessionNote = err;
}

static void openDemoDoc() {
    doc = Session::demo();
    hasDoc = true;
    docPath.clear();
}

static void drawLeftPanel() {
    ImGui::BeginChild("left", ImVec2(280, 0), true);
    ImGui::TextUnformatted(T("SESSION"));
    ImGui::Separator();
    if (!hasDoc) {
        ImGui::TextDisabled("%s", T("No project open yet."));
        ImGui::TextDisabled("%s", T("File > New to begin."));
    } else {
        ImGui::Text("%s", doc.name.c_str());
        ImGui::Text("%s: %.1f BPM  %d/%d", T("Tempo"), doc.time.tempoBpm, doc.time.meter.num, doc.time.meter.den);
        ImGui::Spacing();
        ImGui::TextUnformatted(T("Tracks"));
        for (const auto& t : doc.tracks) ImGui::BulletText("%s (%zu clips)", t.name.c_str(), t.clips.size());
        ImGui::Spacing();
        ImGui::TextUnformatted(T("Markers"));
        for (const auto& m : doc.markers) ImGui::BulletText("%s @ %lld", m.name.c_str(), (long long)m.start);
        if (!sessionNote.empty()) ImGui::TextWrapped("%s", sessionNote.c_str());
    }
    ImGui::Spacing();
    if (ImGui::Button(T("Import Audio..."), ImVec2(-1, 0))) openImportAudioDialog();
    ImGui::Spacing();
    ImGui::TextUnformatted(T("AUDIO"));
    ImGui::Separator();
    ImGui::TextWrapped("%s", audioNote.c_str());
    ImGui::EndChild();
}

static void drawEmptyCenter() {
    ImGui::BeginChild("edit_empty", ImVec2(0, 0), true);
    ImGui::TextDisabled("%s", T("Tracks and the timeline arrive in the next phases."));
    ImGui::TextDisabled("%s", T("File > New to begin."));
    ImGui::EndChild();
}

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

static void drawTransport() {
    if (!hasDoc) return;
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y - 48));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 48));
    ImGui::Begin("transport", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    if (ImGui::Button(Playback::isPlaying() ? T("Stop") : T("Play"))) {
        if (Playback::isPlaying()) Playback::stop();
        else Playback::play();
    }
    ImGui::SameLine();
    if (ImGui::Button(T("Loop"))) Playback::toggleLoop();
    ImGui::SameLine();
    ImGui::Text("%s %lld  %s %s", T("Playhead"), (long long)Playback::playhead(), T("Loop"), Playback::loopEnabled() ? T("on") : T("off"));
    ImGui::End();
}

void frame() {
    if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_I)) {
        openImportAudioDialog();
    }
    if (hasDoc) {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (ImGui::GetIO().KeyShift)
                redoDocument();
            else
                undoDocument();
        }
        Playback::setDocument(&doc);
        const auto plan = Mix::currentPlan();
        if (plan && !plan->missingNote.empty() && sessionNote.empty()) sessionNote = plan->missingNote;
    }
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu(T("File"))) {
            if (ImGui::MenuItem(T("New"), "Ctrl+N")) newDoc();
            if (ImGui::MenuItem(T("Open demo"), nullptr)) openDemoDoc();
            if (ImGui::MenuItem(T("Save"), "Ctrl+S", false, hasDoc)) saveDoc();
            ImGui::Separator();
            if (ImGui::MenuItem(T("Import Audio..."), "Ctrl+Shift+I")) openImportAudioDialog();
            if (ImGui::BeginMenu(T("Import"))) {
                if (ImGui::MenuItem(T("Audio..."), "Ctrl+Shift+I")) openImportAudioDialog();
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(T("Preferences..."))) showPrefs = true;
            ImGui::Separator();
            if (ImGui::MenuItem(T("Quit"), "Alt+F4")) quit = true;
            ImGui::EndMenu();
        }
        if (hasDoc && ImGui::BeginMenu(T("Edit"))) {
            const auto run = [&](const char* label, auto fn) {
                if (ImGui::MenuItem(label)) mutateDocument(label, [&](Session::Document& d) { fn(d); });
            };
            if (ImGui::MenuItem(T("Undo"), "Ctrl+Z", false, hist.canUndo())) undoDocument();
            if (ImGui::MenuItem(T("Redo"), "Ctrl+Shift+Z", false, hist.canRedo())) redoDocument();
            ImGui::Separator();
            run("Cut", Editor::cut);
            if (ImGui::MenuItem(T("Copy"))) Editor::copy(doc);
            run("Paste", [](Session::Document& d) { Editor::paste(d, Playback::playhead(), Editor::selection().trackId); });
            run("Duplicate", Editor::duplicateSelection);
            ImGui::Separator();
            run("Separate", [](Session::Document& d) { Editor::separateAtPlayhead(d, Playback::playhead()); });
            run("Heal", Editor::healSeparation);
            run("Consolidate", Editor::consolidateSelection);
            if (ImGui::MenuItem("Nudge +")) mutateDocument("Nudge", [](Session::Document& d) { Editor::nudgeSelection(d, Editor::gridStepSamples(d) / 4); });
            if (ImGui::MenuItem("Nudge -")) mutateDocument("Nudge", [](Session::Document& d) { Editor::nudgeSelection(d, -Editor::gridStepSamples(d) / 4); });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(T("Help"))) {
            if (ImGui::MenuItem(T("About Polyhymnia"))) showAbout = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    const float transportH = hasDoc ? 48.0f : 0.0f;
    const float chromeH = ImGui::GetFrameHeight() + transportH;
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y - chromeH));
    ImGui::Begin("workspace", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    drawLeftPanel();
    ImGui::SameLine();
    if (hasDoc) {
        const float clipsPanelW = 240.0f;
        const float centerW = std::max(120.0f, ImGui::GetContentRegionAvail().x - clipsPanelW - 8.0f);
        ImGui::BeginChild("edit_main", ImVec2(centerW, 0), false);
        Edit::drawToolbar();
        Edit::draw(doc, 44.0f);
        ImGui::EndChild();
        ImGui::SameLine();
        ClipsPanel::draw(doc, 0.0f);
    } else
        drawEmptyCenter();
    ImGui::End();
    drawPrefs();
    drawAbout();
    drawImportAudioDialog();
    if (hasDoc) {
        Spot::draw(doc);
        ClipsPanel::drawDialogs(doc);
    }
    gImports.show();
    drawTransport();
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
        doc = Session::demo();
        doc.folder = prefDir;
        std::string err;
        const std::string adir = prefDir + "audio";
        std::filesystem::create_directories(adir);
        const int rate = (int)doc.sampleRate;
        Mix::writeTestToneWav(adir + "/sine_440.wav", 440.0, 2.0, rate, err);
        Mix::writeTestToneWav(adir + "/click.wav", 2200.0, 0.08, rate, err);
        hasDoc = true;
        Edit::scriptSetup(doc);
        Playback::setDocument(&doc);
        fprintf(stderr, "edit: timeline %zu tracks, zoom ready\n", doc.tracks.size());
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

        // 1. Import via File > Import Audio onto a new track at session start, copying into Audio Files/
        runImportPaths({wavPath}, 0, 0, true);
        // 2. Drop AIFF file onto Track 1 at 1.75s (84000 samples)
        const uint64_t track1Num = doc.tracks.empty() ? 0 : doc.tracks[0].numId;
        runImportPaths({aiffPath}, 1, 0, true, track1Num, 84000);
        // 3. Drop FLAC file onto empty timeline space (new track) at 0.5s (24000 samples)
        runImportPaths({flacPath}, 0, 0, true, std::nullopt, 24000);
        // 4. Drop WAV file onto the Clips list on the right (whole-file source only)
        runImportPaths({clipListPath}, 2, 0, true);

        Edit::scriptSetup(doc);
        Playback::setDocument(&doc);
        fprintf(stderr, "import: %zu sources in Clips list, %zu tracks, Audio Files copied\n",
                doc.sources.size(), doc.tracks.size());
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

}