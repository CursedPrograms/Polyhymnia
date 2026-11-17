// Polyhymnia - music studio. SDL2, OpenGL 3, Dear ImGui. miniaudio for the device.
//   Polyhymnia [--script NAME] [--capture shot.bmp]
#include "app.h"
#include "audio.h"
#include "lang.h"
#include "mix.h"
#include "playback.h"
#include "project.h"
#include "splash.h"
#include "test_runner.h"

void registerTimeGridTests();
void registerEngineEditTests();
void registerClipModelTests();
void registerCmdEditTests();
void registerCmdEditMoreTests();
void registerCmdClipTests();
void registerCmdClipMoreTests();

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <cstring>
#include <filesystem>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
static bool wantsSelftest() {
    const char* cmd = GetCommandLineA();
    return cmd && std::strstr(cmd, "--selftest");
}
#endif

static bool fileExists(const char* f) {
    if (FILE* t = fopen(f, "rb")) { fclose(t); return true; }
    return false;
}

static ImFont* addFont(const char* file, float size) {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar ranges[] = {0x0020, 0x024F, 0x0400, 0x04FF, 0x2190, 0x21FF, 0x2600, 0x26FF, 0};
    ImFont* font = io.Fonts->AddFontFromFileTTF(file, size, nullptr, ranges);
    static const char* symbols = "C:/Windows/Fonts/seguisym.ttf";
    static const ImWchar symbolRanges[] = {0x2190, 0x21FF, 0x2600, 0x26FF, 0};
    if (fileExists(symbols)) {
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(symbols, size, &merge, symbolRanges);
    }
    return font;
}

static void loadFont() {
    ImGuiIO& io = ImGui::GetIO();
    const char* fonts[] = {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/System/Library/Fonts/Helvetica.ttc"};
    for (const char* f : fonts) {
        if (!fileExists(f)) continue;
        addFont(f, 16.0f);
        const char* light = fileExists("C:/Windows/Fonts/segoeuil.ttf") ? "C:/Windows/Fonts/segoeuil.ttf" : f;
        Splash::setFonts(addFont(light, 48.0f), addFont(f, 13.0f));
        if (Lang::needsCJK()) {
            ImFontConfig m;
            m.MergeMode = true;
            const char* cjk = Lang::current() == "ko" ? "C:/Windows/Fonts/malgun.ttf" : Lang::current() == "zh" ? "C:/Windows/Fonts/msyh.ttc" : "C:/Windows/Fonts/YuGothM.ttc";
            const ImWchar* r = Lang::current() == "ko" ? io.Fonts->GetGlyphRangesKorean() : Lang::current() == "zh" ? io.Fonts->GetGlyphRangesChineseSimplifiedCommon() : io.Fonts->GetGlyphRangesJapanese();
            if (fileExists(cjk)) io.Fonts->AddFontFromFileTTF(cjk, 16.0f, &m, r);
        }
        return;
    }
    io.Fonts->AddFontDefault();
}

static SDL_Window* splashWin = nullptr;
static Uint32 splashStart = 0;
static bool splashSkip = false, quitEarly = false;

static void splashSay(const char* status, float progress) {
    if (!splashWin) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT) quitEarly = true;
        if (e.type == SDL_MOUSEBUTTONDOWN || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) splashSkip = true;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    Splash::draw(ImGui::GetBackgroundDrawList(), 0, 0, status, progress);
    ImGui::Render();
    int w, h;
    SDL_GL_GetDrawableSize(splashWin, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(splashWin);
}

static std::string dirSlash(const char* d) {
    std::string s = d ? d : "";
    if (!s.empty() && s.back() != '/' && s.back() != '\\') s += "/";
    return s;
}

static int runAudioTest(const char* outDir) {
    const std::string dir = dirSlash(outDir);
    const std::string tonePath = dir + "test_tone.wav";
    const std::string projectPath = dir + "tone-test.polyhymnia";
    const std::string renderPath = dir + "tone-render.wav";
    std::string err;
    const int rate = 48000;
    if (!Mix::writeTestToneWav(tonePath, 1000.0, 2.0, rate, err)) {
        fprintf(stderr, "audiotest: %s\n", err.c_str());
        return 1;
    }
    Session::Document doc = Session::empty();
    doc.name = "Tone test";
    doc.sampleRate = rate;
    Session::Source tone;
    tone.id = "tone";
    tone.path = "test_tone.wav";
    tone.sampleRate = rate;
    tone.channels = 2;
    tone.frames = (long long)rate * 2;
    doc.sources.push_back(tone);
    Session::Track tr;
    tr.id = "t1";
    tr.name = "Tone";
    Session::Clip tc;
    tc.id = "c1";
    tc.sourceId = "tone";
    tc.length = (long long)rate * 2;
    tr.clips.push_back(tc);
    doc.tracks.push_back(std::move(tr));
    doc.touch = 1;
    doc.folder = dir;
    if (!Project::save(doc, projectPath, err)) {
        fprintf(stderr, "audiotest: %s\n", err.c_str());
        return 1;
    }
    if (!Mix::renderToWav(doc, renderPath, err)) {
        fprintf(stderr, "audiotest: %s\n", err.c_str());
        return 1;
    }
    if (!Mix::verifyToneInWav(renderPath, 1000.0, rate, err)) {
        fprintf(stderr, "audiotest: %s\n", err.c_str());
        return 1;
    }
    fprintf(stderr, "audiotest: 1 kHz tone ok, length %d samples\n", rate * 2);
    return 0;
}

static int runRender(const char* projectPath, const char* outWav) {
    Session::Document doc;
    std::string err;
    if (!Project::load(projectPath, doc, err)) {
        fprintf(stderr, "render: %s\n", err.c_str());
        return 1;
    }
    if (!Mix::renderToWav(doc, outWav, err)) {
        fprintf(stderr, "render: %s\n", err.c_str());
        return 1;
    }
    fprintf(stderr, "render: wrote %s (%lld samples)\n", outWav, (long long)Mix::sessionLength(doc));
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    if (wantsSelftest()) {
        registerEngineEditTests();
        registerClipModelTests();
        registerCmdEditTests();
        registerCmdEditMoreTests();
        registerCmdClipTests();
        registerCmdClipMoreTests();
        registerTimeGridTests(); // after edit+clip so a grid crash does not block engine results
        return Test::runAll();
    }
#endif
    std::string capture, scriptName;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--selftest") {
#ifdef _WIN32
            AllocConsole();
            FILE* con = nullptr;
            freopen_s(&con, "CONOUT$", "w", stderr);
            freopen_s(&con, "CONOUT$", "w", stdout);
#endif
            registerEngineEditTests();
            registerClipModelTests();
            registerCmdEditTests();
            registerCmdEditMoreTests();
            registerCmdClipTests();
            registerCmdClipMoreTests();
            registerTimeGridTests();
            return Test::runAll();
        }
        if (a == "--capture" && i + 1 < argc) capture = argv[++i];
        else if (a == "--script" && i + 1 < argc) scriptName = argv[++i];
        else if (a == "--render" && i + 2 < argc) {
            const char* projectPath = argv[++i];
            const char* outWav = argv[++i];
            return runRender(projectPath, outWav);
        }
        else if (a == "--audiotest" && i + 1 < argc) return runAudioTest(argv[++i]);
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (capture.empty() ? 0 : SDL_WINDOW_HIDDEN);
    SDL_Window* win = SDL_CreateWindow("Polyhymnia", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, flags);
    if (!win) return 1;
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);
    char* bp = SDL_GetBasePath();
    const std::string base = bp ? bp : "";
    SDL_free(bp);
    SDL_Surface* icon = SDL_LoadBMP((base + "assets/icon.bmp").c_str());
    if (icon) SDL_SetWindowIcon(win, icon);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    loadFont();

    if (capture.empty()) {
        splashWin = SDL_CreateWindow("Polyhymnia", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)Splash::W, (int)Splash::H,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
        if (splashWin) SDL_GL_MakeCurrent(splashWin, gl);
        if (splashWin && icon) SDL_SetWindowIcon(splashWin, icon);
    }
    if (icon) SDL_FreeSurface(icon);
    ImGui_ImplSDL2_InitForOpenGL(splashWin ? splashWin : win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    splashStart = SDL_GetTicks();
    splashSay("Loading...", 0.1f);

    const std::string langDir = base + "lang/";
    char* pp = SDL_GetPrefPath("Cursed Entertainment", "Polyhymnia");
    App::init(win, pp ? pp : "", langDir, splashWin ? splashSay : nullptr);
    SDL_free(pp);

    if (splashWin) {
        for (;;) {
            const Uint32 t = SDL_GetTicks() - splashStart;
            if (splashSkip || quitEarly || t >= 1200) break;
            splashSay("Ready", 1.0f);
        }
        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(splashWin);
        splashWin = nullptr;
        SDL_GL_MakeCurrent(win, gl);
        ImGui_ImplSDL2_InitForOpenGL(win, gl);
    }
    if (!capture.empty()) SDL_ShowWindow(win);
    else {
        SDL_ShowWindow(win);
        SDL_RaiseWindow(win);
    }
    if (quitEarly) App::requestQuit();

    if (!capture.empty() && !scriptName.empty()) App::script(scriptName);

    int frames = 0;
    while (!App::wantsQuit()) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) App::requestQuit();
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        App::frame();
        ImGui::Render();
        int w, h;
        SDL_GL_GetDrawableSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frames;
        if (!capture.empty()) {
            if (frames >= 4) {
                std::vector<unsigned char> px((size_t)w * h * 4);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                std::vector<unsigned char> flip(px.size());
                for (int y = 0; y < h; ++y) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flip.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
                SDL_SaveBMP(s, capture.c_str());
                SDL_FreeSurface(s);
                break;
            }
        }
        SDL_GL_SwapWindow(win);
    }

    Audio::close();
    Playback::setDocument(nullptr);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}