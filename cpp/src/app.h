// Polyhymnia - main window: menus, preferences, transport placeholders.
#pragma once
#include <SDL.h>
#include <string>
#include <functional>

namespace App {
using SplashFn = std::function<void(const char*, float)>;
void init(SDL_Window* win, const std::string& prefPath, const std::string& languageDir, SplashFn splash);
void frame();
void script(const std::string& name);
bool wantsQuit();
void requestQuit();
void setCaptureHints(bool prefsOpen, int prefsPage);
}