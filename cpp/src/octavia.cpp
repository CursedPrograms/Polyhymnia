// Polyhymnia - Octavia stock plugin downloader and installer (after AGENTS.md).
#include "octavia.h"
#include "vst3_host.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Octavia {

namespace {

std::atomic<Status> gStatus{Status::Idle};
std::atomic<float> gProgress{0.0f};
std::string gMessage;
std::mutex gMsgMtx;

const std::vector<PluginSpec> kSpecs = {
    {"Safety Pin", "SafetyPin", "D:/cc/SafetyPin/build/SafetyPin_artefacts/Release/VST3/Safety Pin.vst3"},
    {"Fuzz14", "Fuzz14", "D:/cc/Fuzz14/build/Fuzz14_artefacts/Release/VST3/Fuzz14.vst3"},
    {"stringtuner", "stringtuner", "D:/cc/stringtuner/build/stringtuner_artefacts/Release/VST3/stringtuner.vst3"},
    {"Solderless", "Solderless", "D:/cc/Solderless/build"},
};

void setMessage(std::string msg) {
    std::lock_guard<std::mutex> lk(gMsgMtx);
    gMessage = std::move(msg);
}

} // namespace

const std::vector<PluginSpec>& stockPlugins() {
    return kSpecs;
}

fs::path installDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return fs::path(buf) / "Programs" / "Common" / "VST3" / "Octavia";
    }
    len = GetEnvironmentVariableA("COMMONPROGRAMFILES", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return fs::path(buf) / "VST3" / "Octavia";
    }
#endif
    std::error_code ec;
    return fs::temp_directory_path(ec) / "OctaviaVSTs";
}

bool areInstalled() {
    std::error_code ec;
    const fs::path dir = installDir();
    if (!fs::exists(dir, ec)) return false;
    for (const auto& spec : kSpecs) {
        fs::path p = dir / (spec.name + ".vst3");
        if (fs::exists(p, ec)) return true;
    }
    return false;
}

Status currentStatus() { return gStatus.load(); }
float currentProgress() { return gProgress.load(); }

std::string statusMessage() {
    std::lock_guard<std::mutex> lk(gMsgMtx);
    return gMessage;
}

void startInstallAsync(std::function<void(bool, const std::string&)> onComplete) {
    if (gStatus.load() == Status::Downloading || gStatus.load() == Status::Installing) {
        return;
    }

    gStatus = Status::Checking;
    gProgress = 0.1f;
    setMessage("Checking plugin sources...");

    std::thread([onComplete]() {
        std::error_code ec;
        fs::path dstDir = installDir();
        fs::create_directories(dstDir, ec);

        size_t installedCount = 0;
        const size_t total = kSpecs.size();

        for (size_t i = 0; i < total; ++i) {
            const auto& spec = kSpecs[i];
            gStatus = Status::Installing;
            gProgress = 0.2f + 0.7f * (static_cast<float>(i) / static_cast<float>(total));
            setMessage("Installing " + spec.name + "...");

            fs::path localDev(spec.localDevPath);
            if (fs::exists(localDev, ec)) {
                fs::path target = dstDir / localDev.filename();
                fs::remove_all(target, ec);
                fs::copy(localDev, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    ++installedCount;
                    continue;
                }
            }

            // Fallback or online: check dist or prebuilt zip beside repo
            fs::path repoDir = fs::path("D:/cc") / spec.repo / "dist";
            if (fs::exists(repoDir, ec)) {
                for (const auto& entry : fs::directory_iterator(repoDir, ec)) {
                    if (entry.path().extension() == ".zip") {
                        // Unzip to dstDir
#ifdef _WIN32
                        std::string cmd = "powershell -NoProfile -Command \"Expand-Archive -Path '" +
                            entry.path().string() + "' -DestinationPath '" + dstDir.string() + "' -Force\"";
                        std::system(cmd.c_str());
                        ++installedCount;
                        break;
#endif
                    }
                }
            }
        }

        Vst3Host::addExtraPath(dstDir);
        Vst3Host::rescan();

        gProgress = 1.0f;
        if (installedCount > 0) {
            gStatus = Status::Installed;
            setMessage("Installed " + std::to_string(installedCount) + " Octavia plugin(s)");
            if (onComplete) onComplete(true, "Installed " + std::to_string(installedCount) + " Octavia plugin(s)");
        } else {
            gStatus = Status::Error;
            setMessage("Could not find or install Octavia plugins");
            if (onComplete) onComplete(false, "Could not find or install Octavia plugins");
        }
    }).detach();
}

} // namespace Octavia
