// Polyhymnia - VST3 plugin discovery and bundle resolution
// (after SoundCraft crates/vst3-host/src/scan.rs).
#include "vst3_scan.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Vst3Host {

namespace {

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string trimAscii(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

std::optional<std::string> getEnv(const char* name) {
#ifdef _WIN32
    char buf[32767];
    DWORD len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) return std::string(buf, len);
    return std::nullopt;
#else
    const char* v = std::getenv(name);
    if (v && *v) return std::string(v);
    return std::nullopt;
#endif
}

std::vector<fs::path> splitPathList(const std::string& list) {
    std::vector<fs::path> out;
#ifdef _WIN32
    const char delimiter = ';';
#else
    const char delimiter = ':';
#endif
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        std::string trimmed = trimAscii(item);
        if (!trimmed.empty()) {
            out.emplace_back(fs::u8path(trimmed));
        }
    }
    return out;
}

std::vector<const char*> archDirs(Os os) {
    switch (os) {
    case Os::MacOs:
        return {"MacOS"};
    case Os::Windows:
#if defined(_M_ARM64) || defined(__aarch64__)
        return {"arm64-win", "arm64ec-win", "arm64x-win"};
#elif defined(_M_IX86) || defined(__i386__)
        return {"x86-win"};
#else
        return {"x86_64-win"};
#endif
    case Os::Unix:
#if defined(__aarch64__)
        return {"aarch64-linux"};
#elif defined(__i386__)
        return {"i386-linux"};
#else
        return {"x86_64-linux"};
#endif
    }
    return {};
}

void walk(const fs::path& dir, size_t depth, std::vector<fs::path>& out, size_t& budget) {
    if (depth > kMaxDepth || out.size() >= kMaxBundles || budget == 0) return;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::follow_directory_symlink, ec);
    if (ec) return;
    for (const auto& entry : it) {
        if (budget == 0 || out.size() >= kMaxBundles) return;
        --budget;
        const fs::path p = entry.path();
        if (isVst3Path(p)) {
            out.push_back(p);
        } else {
            std::error_code statusEc;
            if (fs::is_directory(p, statusEc) && !statusEc) {
                walk(p, depth + 1, out, budget);
            }
        }
    }
}

} // namespace

Os currentOs() {
#if defined(__APPLE__)
    return Os::MacOs;
#elif defined(_WIN32)
    return Os::Windows;
#else
    return Os::Unix;
#endif
}

// after SoundCraft (crates/vst3-host/src/scan.rs is_vst3_path)
bool isVst3Path(const fs::path& p) {
    const std::string ext = p.extension().string();
    return iequals(ext, ".vst3");
}

// after SoundCraft (crates/vst3-host/src/scan.rs os_search_paths)
std::vector<fs::path> osSearchPaths(Os os,
                                    const std::optional<fs::path>& home,
                                    const std::optional<fs::path>& commonProgramFiles,
                                    const std::optional<fs::path>& localAppData) {
    std::vector<fs::path> v;
    switch (os) {
    case Os::MacOs:
        if (home.has_value()) {
            v.push_back(*home / "Library" / "Audio" / "Plug-Ins" / "VST3");
        }
        v.push_back(fs::path("/Library/Audio/Plug-Ins/VST3"));
        break;
    case Os::Windows:
        if (commonProgramFiles.has_value()) {
            v.push_back(*commonProgramFiles / "VST3");
        }
        if (localAppData.has_value()) {
            v.push_back(*localAppData / "Programs" / "Common" / "VST3");
        }
        break;
    case Os::Unix:
        if (home.has_value()) {
            v.push_back(*home / ".vst3");
        }
        v.push_back(fs::path("/usr/lib/vst3"));
        v.push_back(fs::path("/usr/local/lib/vst3"));
        break;
    }
    return v;
}

// after SoundCraft (crates/vst3-host/src/scan.rs default_search_paths)
std::vector<fs::path> defaultSearchPaths() {
    std::optional<fs::path> home;
    if (auto h = getEnv("HOME")) home = fs::u8path(*h);
    else if (auto h = getEnv("USERPROFILE")) home = fs::u8path(*h);

    std::optional<fs::path> common;
    if (auto c = getEnv("COMMONPROGRAMFILES")) common = fs::u8path(*c);

    std::optional<fs::path> local;
    if (auto l = getEnv("LOCALAPPDATA")) local = fs::u8path(*l);

    auto v = osSearchPaths(currentOs(), home, common, local);
    if (auto vp = getEnv("VST3_PATH")) {
        auto extra = splitPathList(*vp);
        v.insert(v.end(), extra.begin(), extra.end());
    }
    return v;
}

// after SoundCraft (crates/vst3-host/src/scan.rs find_bundles)
std::vector<fs::path> findBundles(const std::vector<fs::path>& dirs) {
    std::vector<fs::path> out;
    size_t budget = kMaxEntries;
    for (const auto& d : dirs) {
        std::error_code ec;
        if (isVst3Path(d) && fs::exists(d, ec)) {
            out.push_back(d);
        } else {
            walk(d, 0, out, budget);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.size() > kMaxBundles) {
        out.resize(kMaxBundles);
    }
    return out;
}

// after SoundCraft (crates/vst3-host/src/scan.rs bundle_binary_for)
std::optional<fs::path> bundleBinaryFor(Os os, const fs::path& path) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec) && !ec) {
        return path;
    }
    if (!fs::is_directory(path, ec) || ec) {
        return std::nullopt;
    }
    const fs::path stem = path.stem();
    const char* ext = nullptr;
    switch (os) {
    case Os::MacOs:
        ext = nullptr;
        break;
    case Os::Windows:
        ext = ".vst3";
        break;
    case Os::Unix:
        ext = ".so";
        break;
    }

    for (const char* dirName : archDirs(os)) {
        const fs::path folder = path / "Contents" / dirName;
        fs::path named = folder / stem;
        if (ext) named.replace_extension(ext);
        if (fs::is_regular_file(named, ec) && !ec) {
            return named;
        }

        fs::directory_iterator it(folder, ec);
        if (ec) continue;
        std::vector<fs::path> files;
        for (const auto& entry : it) {
            const fs::path p = entry.path();
            if (fs::is_regular_file(p, ec) && !ec) {
                if (!ext || iequals(p.extension().string(), ext)) {
                    files.push_back(p);
                }
            }
            if (files.size() >= 64) break;
        }
        std::sort(files.begin(), files.end());
        if (!files.empty()) {
            return files.front();
        }
    }
    return std::nullopt;
}

// after SoundCraft (crates/vst3-host/src/scan.rs bundle_binary)
std::optional<fs::path> bundleBinary(const fs::path& path) {
    return bundleBinaryFor(currentOs(), path);
}

// after SoundCraft (crates/vst3-host/src/scan.rs split_sub_categories)
std::vector<std::string> splitSubCategories(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, '|')) {
        std::string trimmed = trimAscii(token);
        if (!trimmed.empty()) {
            out.push_back(trimmed);
            if (out.size() >= 32) break;
        }
    }
    return out;
}

// after SoundCraft (crates/vst3-host/src/scan.rs is_instrument)
bool isInstrument(const std::vector<std::string>& subs) {
    return !subs.empty() && iequals(subs.front(), "Instrument");
}

// after SoundCraft (crates/vst3-host/src/scan.rs category_from_sub_categories)
Dsp::Category categoryFromSubCategories(const std::vector<std::string>& subs) {
    auto has = [&](const std::vector<const char*>& names) {
        for (const auto& sub : subs) {
            for (const char* name : names) {
                if (iequals(sub, name)) return true;
            }
        }
        return false;
    };

    if (isInstrument(subs)) {
        return Dsp::Category::Instrument;
    } else if (has({"EQ", "Filter"})) {
        return Dsp::Category::Eq;
    } else if (has({"Dynamics"})) {
        return Dsp::Category::Dynamics;
    } else if (has({"Reverb"})) {
        return Dsp::Category::Reverb;
    } else if (has({"Delay"})) {
        return Dsp::Category::Delay;
    } else if (has({"Modulation"})) {
        return Dsp::Category::Modulation;
    } else if (has({"Distortion"})) {
        return Dsp::Category::Harmonic;
    } else if (has({"Pitch Shift"})) {
        return Dsp::Category::PitchShift;
    }
    return Dsp::Category::Other;
}

} // namespace Vst3Host
