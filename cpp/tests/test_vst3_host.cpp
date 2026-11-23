// Polyhymnia - VST3 plugin hosting unit tests
// (after SoundCraft crates/vst3-host/src/lib.rs & src/ffi.rs tests).
#include "test_runner.h"
#include "vst3_host.h"

#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>

#include <algorithm>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <process.h>
#define GETPID _getpid
#else
#include <unistd.h>
#define GETPID getpid
#endif

namespace {

namespace fs = std::filesystem;

fs::path tempDir(const char* name) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) /
        ("polyhymnia-vst3-test-" + std::string(name) + "-" + std::to_string(GETPID()));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// after SoundCraft (crates/vst3-host/src/lib.rs tests::ids_parse)
bool testIdsParse() {
    const char* id = "vst3:0123456789ABCDEFfedcba9876543210";
    auto parsed = Vst3Host::parseId(id);
    EXPECT_TRUE(parsed.has_value());
    const uint8_t expected[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
    EXPECT_EQ(std::memcmp(parsed->data(), expected, 16), 0);

    EXPECT_TRUE(!Vst3Host::parseId("vst3:").has_value());
    EXPECT_TRUE(!Vst3Host::parseId("vst3:0123").has_value());
    EXPECT_TRUE(!Vst3Host::parseId("vst3:0123456789ABCDEF0123456789ABCDEG").has_value());
    EXPECT_TRUE(!Vst3Host::parseId("vst3:\xc3\xa9\xc3\xa9").has_value());
    EXPECT_TRUE(!Vst3Host::parseId("VST3:0123456789ABCDEF0123456789ABCDEF").has_value());
    EXPECT_TRUE(!Vst3Host::parseId("clap:x").has_value());
    EXPECT_TRUE(!Vst3Host::parseId("eq_7band").has_value());
    return true;
}

// after SoundCraft (crates/vst3-host/src/lib.rs tests::non_vst3_ids_are_rejected_without_scanning)
bool testNonVst3IdsAreRejectedWithoutScanning() {
    EXPECT_TRUE(Vst3Host::createInstance("eq_7band") == nullptr);
    EXPECT_TRUE(Vst3Host::createInstance("clap:com.x") == nullptr);
    EXPECT_TRUE(Vst3Host::findDescriptor("reverb") == nullptr);
    return true;
}

// after SoundCraft (crates/vst3-host/src/ffi.rs tests::state_container_parses_strictly)
bool testStateContainerParsesStrictly() {
    std::vector<uint8_t> comp = {'a', 'b', 'c'};
    std::vector<uint8_t> ctrl = {'x', 'y'};
    auto blob = Vst3Host::makeStateBlob(comp, ctrl);

    auto parts = Vst3Host::splitState(blob.data(), blob.size());
    EXPECT_TRUE(parts.has_value());
    EXPECT_EQ(parts->first, comp);
    EXPECT_EQ(parts->second, ctrl);

    const char* raw = "raw component";
    auto rawParts = Vst3Host::splitState(reinterpret_cast<const uint8_t*>(raw), std::strlen(raw));
    EXPECT_TRUE(rawParts.has_value());
    EXPECT_EQ(rawParts->first, std::vector<uint8_t>(raw, raw + std::strlen(raw)));
    EXPECT_TRUE(rawParts->second.empty());

    EXPECT_TRUE(!Vst3Host::splitState(nullptr, 0).has_value());

    auto trailing = blob;
    trailing.push_back(0);
    EXPECT_TRUE(!Vst3Host::splitState(trailing.data(), trailing.size()).has_value());

    EXPECT_TRUE(!Vst3Host::splitState(blob.data(), blob.size() - 1).has_value());

    std::vector<uint8_t> huge = {'S', 'C', 'V', '3'};
    uint32_t maxU32 = 0xFFFFFFFF;
    huge.insert(huge.end(), reinterpret_cast<uint8_t*>(&maxU32), reinterpret_cast<uint8_t*>(&maxU32) + 4);
    EXPECT_TRUE(!Vst3Host::splitState(huge.data(), huge.size()).has_value());

    const char* magicOnly = "SCV3";
    EXPECT_TRUE(!Vst3Host::splitState(reinterpret_cast<const uint8_t*>(magicOnly), 4).has_value());
    return true;
}

// after SoundCraft (crates/vst3-host/src/lib.rs tests::garbage_files_are_errors_not_crashes)
bool testGarbageFilesAreErrorsNotCrashes() {
    std::error_code ec;
    const fs::path d = tempDir("garbage");
    const fs::path winArch = d / "Bundle.vst3" / "Contents" / "x86_64-win";
    fs::create_directories(winArch, ec);

    std::ofstream(d / "garbage.vst3") << "\x7f" << "ELF not a plugin";
    std::ofstream(d / "empty.vst3");
    std::ofstream(winArch / "Bundle.vst3") << "\xcf\xfa\xed\xfe\x01\x02\x03";
    fs::create_directories(d / "NoBinary.vst3" / "Contents", ec);

    Vst3Host::addExtraPath(d);
    auto scanned = Vst3Host::rescan();
    (void)scanned;

    const char* missing = "vst3:00000000000000000000000000000000";
    EXPECT_TRUE(Vst3Host::createInstance(missing) == nullptr);
    EXPECT_TRUE(Vst3Host::findDescriptor(missing) == nullptr);

    fs::remove_all(d, ec);
    return true;
}

} // namespace

void registerVst3HostTests() {
    Test::add("vst3_ids_parse", testIdsParse);
    Test::add("vst3_non_vst3_ids_are_rejected_without_scanning", testNonVst3IdsAreRejectedWithoutScanning);
    Test::add("vst3_state_container_parses_strictly", testStateContainerParsesStrictly);
    Test::add("vst3_garbage_files_are_errors_not_crashes", testGarbageFilesAreErrorsNotCrashes);
}
