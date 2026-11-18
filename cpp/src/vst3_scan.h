// Polyhymnia - VST3 plugin discovery and bundle resolution
// (after SoundCraft crates/vst3-host/src/scan.rs).
#pragma once

#include "dsp.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Vst3Host {

namespace fs = std::filesystem;

// after SoundCraft (crates/vst3-host/src/scan.rs Os)
enum class Os {
    MacOs,
    Windows,
    Unix,
};

Os currentOs();

// Directory recursion limit while scanning (vendors nest folders, never deeply).
constexpr size_t kMaxDepth = 8;
// Upper bound on bundles collected by one scan (hostile or huge folders).
constexpr size_t kMaxBundles = 4096;
// Upper bound on directory entries visited by one scan.
constexpr size_t kMaxEntries = 100000;

// after SoundCraft (crates/vst3-host/src/scan.rs is_vst3_path)
bool isVst3Path(const fs::path& p);

// after SoundCraft (crates/vst3-host/src/scan.rs os_search_paths)
std::vector<fs::path> osSearchPaths(Os os,
                                    const std::optional<fs::path>& home,
                                    const std::optional<fs::path>& commonProgramFiles,
                                    const std::optional<fs::path>& localAppData);

// after SoundCraft (crates/vst3-host/src/scan.rs default_search_paths)
std::vector<fs::path> defaultSearchPaths();

// after SoundCraft (crates/vst3-host/src/scan.rs find_bundles)
std::vector<fs::path> findBundles(const std::vector<fs::path>& dirs);

// after SoundCraft (crates/vst3-host/src/scan.rs bundle_binary_for)
std::optional<fs::path> bundleBinaryFor(Os os, const fs::path& path);

// after SoundCraft (crates/vst3-host/src/scan.rs bundle_binary)
std::optional<fs::path> bundleBinary(const fs::path& path);

// after SoundCraft (crates/vst3-host/src/scan.rs split_sub_categories)
std::vector<std::string> splitSubCategories(const std::string& s);

// after SoundCraft (crates/vst3-host/src/scan.rs category_from_sub_categories)
Dsp::Category categoryFromSubCategories(const std::vector<std::string>& subs);

// after SoundCraft (crates/vst3-host/src/scan.rs is_instrument)
bool isInstrument(const std::vector<std::string>& subs);

} // namespace Vst3Host
