// Polyhymnia - load and save .polyhymnia project files (text, beside the audio files).
#pragma once
#include "session.h"

#include <string>

namespace Project {
bool save(const Session::Document& doc, const std::string& path, std::string& err);
bool load(const std::string& path, Session::Document& doc, std::string& err);
}