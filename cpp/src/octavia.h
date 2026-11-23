// Polyhymnia - Octavia stock plugin downloader and installer (after AGENTS.md).
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Octavia {

namespace fs = std::filesystem;

enum class Status {
    Idle,
    Checking,
    Downloading,
    Installing,
    Installed,
    Error,
};

struct PluginSpec {
    std::string name;
    std::string repo;
    std::string localDevPath; // e.g. D:/cc/<repo>/build/...
};

const std::vector<PluginSpec>& stockPlugins();

// Returns the target VST3 install directory for Octavia plugins.
fs::path installDir();

// Checks if the Octavia stock plugins are currently installed.
bool areInstalled();

// Status of the background fetch/install process.
Status currentStatus();
float currentProgress();
std::string statusMessage();

// Starts background installation: checks local dev folders first (instant install),
// or downloads releases from GitHub (https://api.github.com/repos/CursedPrograms/<repo>/releases/latest).
void startInstallAsync(std::function<void(bool success, const std::string& msg)> onComplete = nullptr);

} // namespace Octavia
