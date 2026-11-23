// Polyhymnia - VST3 plugin hosting (after SoundCraft crates/vst3-host).
#pragma once

#include "dsp.h"
#include "vst3_scan.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Vst3Host {

namespace fs = std::filesystem;

constexpr const char* kIdPrefix = "vst3:";
constexpr const char* kAudioModuleClass = "Audio Module Class";
constexpr size_t kMaxStateBytes = 64 << 20;

// after SoundCraft (crates/vst3-host/src/lib.rs Vst3Descriptor)
struct Vst3Descriptor {
    std::string id; // vst3:<32 uppercase hex digits>
    std::string classId;
    std::string name;
    std::string vendor;
    std::string version;
    std::string sdkVersion;
    std::vector<std::string> subCategories;
    Dsp::Category category = Dsp::Category::Other;
    bool isInstrument = false;
    fs::path path;
};

// Converts 16-byte class ID to 32 hex uppercase digits.
std::string toHex(const uint8_t cid[16]);

// after SoundCraft (crates/vst3-host/src/lib.rs parse_id)
std::optional<std::array<uint8_t, 16>> parseId(const std::string& id);

// after SoundCraft (crates/vst3-host/src/ffi/instance.rs split_state)
std::optional<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> splitState(const uint8_t* data, size_t size);

// after SoundCraft (crates/vst3-host/src/ffi/instance.rs save_state container SCV3)
std::vector<uint8_t> makeStateBlob(const std::vector<uint8_t>& compState, const std::vector<uint8_t>& ctrlState);

// after SoundCraft (crates/vst3-host/src/registry.rs)
std::vector<Vst3Descriptor> scan();
std::vector<Vst3Descriptor> rescan();
std::optional<std::vector<Vst3Descriptor>> cached();
void addExtraPath(const fs::path& p);
std::vector<fs::path> extraPaths();
const Vst3Descriptor* findDescriptor(const std::string& id);
std::unique_ptr<Dsp::Plugin> createInstance(const std::string& id);

// Registers scanned VST3 plugins with Polyhymnia's central Dsp::plugins() list.
void registerWithDsp();

// Closes all open plugin windows.
void closeAllPluginWindows();

} // namespace Vst3Host
