// Polyhymnia - Edit window shell, preferences (language and audio device), test hooks.
#include "app.h"
#include "audio.h"
#include "lang.h"
#include "edit.h"
#include "mix.h"
#include "playback.h"
#include "project.h"
#include "splash.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

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
    hasDoc = true;
    docPath.clear();
    sessionNote.clear();
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
        for (const auto& m : doc.markers) ImGui::BulletText("%s @ %lld", m.name.c_str(), (long long)m.at);
        if (!sessionNote.empty()) ImGui::TextWrapped("%s", sessionNote.c_str());
    }
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
    if (hasDoc) {
        Playback::setDocument(&doc);
        const auto plan = Mix::currentPlan();
        if (plan && !plan->missingNote.empty()) sessionNote = plan->missingNote;
    }
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu(T("File"))) {
            if (ImGui::MenuItem(T("New"), "Ctrl+N")) newDoc();
            if (ImGui::MenuItem(T("Open demo"), nullptr)) openDemoDoc();
            if (ImGui::MenuItem(T("Save"), "Ctrl+S", false, hasDoc)) saveDoc();
            ImGui::Separator();
            if (ImGui::MenuItem(T("Preferences..."))) showPrefs = true;
            ImGui::Separator();
            if (ImGui::MenuItem(T("Quit"), "Alt+F4")) quit = true;
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
        ImGui::BeginChild("edit_main", ImVec2(0, 0), false);
        Edit::draw(doc, 0);
        ImGui::EndChild();
    } else
        drawEmptyCenter();
    ImGui::End();
    drawPrefs();
    drawAbout();
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

}