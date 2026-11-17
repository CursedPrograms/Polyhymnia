// Polyhymnia - main window: menus, preferences, transport placeholders.
#pragma once
#include "session.h"
#include <SDL.h>
#include <functional>
#include <string>

namespace App {
using SplashFn = std::function<void(const char*, float)>;
void init(SDL_Window* win, const std::string& prefPath, const std::string& languageDir, SplashFn splash);
void frame();
void script(const std::string& name);
void onFileDrop(const std::string& path, float x, float y);
bool wantsQuit();
void requestQuit();
void setCaptureHints(bool prefsOpen, int prefsPage);
const Session::Document& document();
bool hasSession();
bool mutateDocument(const std::string& label, const std::function<void(Session::Document&)>& fn);
bool undoDocument();
bool redoDocument();
}