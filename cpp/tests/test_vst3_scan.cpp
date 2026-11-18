// Polyhymnia - VST3 plugin scanning tests
// (after SoundCraft crates/vst3-host/src/scan.rs tests).
#include "test_runner.h"
#include "vst3_scan.h"

#include <algorithm>
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
        ("polyhymnia-vst3-scan-" + std::string(name) + "-" + std::to_string(GETPID()));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void writeFile(const fs::path& p, const char* content = "x") {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary);
    ofs << content;
}

// after SoundCraft (crates/vst3-host/src/scan.rs tests::finds_files_and_bundles_recursively_but_not_inside_bundles)
bool testFindsFilesAndBundlesRecursivelyButNotInsideBundles() {
    std::error_code ec;
    const fs::path d = tempDir("find");
    writeFile(d / "a.vst3");
    writeFile(d / "readme.txt");
    writeFile(d / "vendor" / "sub" / "B.VST3");
    writeFile(d / "c.vst3" / "Contents" / "MacOS" / "c");
    writeFile(d / "c.vst3" / "Contents" / "inner.vst3");

    const auto found = Vst3Host::findBundles({d, d / "missing"});
    std::vector<std::string> names;
    for (const auto& p : found) {
        auto rel = fs::relative(p, d, ec).generic_string();
        names.push_back(rel);
    }
    std::sort(names.begin(), names.end());
    const std::vector<std::string> expected = {"a.vst3", "c.vst3", "vendor/sub/B.VST3"};
    EXPECT_EQ(names.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < names.size(); ++i) {
        EXPECT_EQ(names[i], expected[i]);
    }

    const auto single = Vst3Host::findBundles({d / "a.vst3"});
    EXPECT_EQ(single.size(), size_t(1));
    EXPECT_EQ(single[0], d / "a.vst3");

    fs::remove_all(d, ec);
    return true;
}

// after SoundCraft (crates/vst3-host/src/scan.rs tests::bundles_resolve_per_os_layout)
bool testBundlesResolvePerOsLayout() {
    std::error_code ec;
    const fs::path d = tempDir("layout");

    // macOS
    writeFile(d / "M.vst3" / "Contents" / "MacOS" / "M");
    writeFile(d / "M.vst3" / "Contents" / "MacOS" / "aaa");
    EXPECT_EQ(Vst3Host::bundleBinaryFor(Vst3Host::Os::MacOs, d / "M.vst3"),
              d / "M.vst3" / "Contents" / "MacOS" / "M");

    // macOS fallback: first file when name differs
    writeFile(d / "N.vst3" / "Contents" / "MacOS" / "other");
    EXPECT_EQ(Vst3Host::bundleBinaryFor(Vst3Host::Os::MacOs, d / "N.vst3"),
              d / "N.vst3" / "Contents" / "MacOS" / "other");

    // Windows
    const std::string winArch = "x86_64-win";
    writeFile(d / "W.vst3" / "Contents" / winArch / "W.vst3");
    writeFile(d / "W.vst3" / "Contents" / winArch / "helper.dll");
    EXPECT_EQ(Vst3Host::bundleBinaryFor(Vst3Host::Os::Windows, d / "W.vst3"),
              d / "W.vst3" / "Contents" / winArch / "W.vst3");

    // Linux
    const std::string linArch = "x86_64-linux";
    writeFile(d / "L.vst3" / "Contents" / linArch / "notes.txt");
    EXPECT_TRUE(!Vst3Host::bundleBinaryFor(Vst3Host::Os::Unix, d / "L.vst3").has_value()); // only .so qualify
    writeFile(d / "L.vst3" / "Contents" / linArch / "Renamed.so");
    EXPECT_EQ(Vst3Host::bundleBinaryFor(Vst3Host::Os::Unix, d / "L.vst3"),
              d / "L.vst3" / "Contents" / linArch / "Renamed.so");
    writeFile(d / "L.vst3" / "Contents" / linArch / "L.so");
    EXPECT_EQ(Vst3Host::bundleBinaryFor(Vst3Host::Os::Unix, d / "L.vst3"),
              d / "L.vst3" / "Contents" / linArch / "L.so");

    // Single-file plugin, wrong layout, missing
    writeFile(d / "single.vst3");
    EXPECT_EQ(Vst3Host::bundleBinaryFor(Vst3Host::Os::Windows, d / "single.vst3"), d / "single.vst3");
    EXPECT_TRUE(!Vst3Host::bundleBinaryFor(Vst3Host::Os::Windows, d / "M.vst3").has_value());
    EXPECT_TRUE(!Vst3Host::bundleBinaryFor(Vst3Host::Os::MacOs, d / "nope.vst3").has_value());

    fs::create_directories(d / "Empty.vst3", ec);
    EXPECT_TRUE(!Vst3Host::bundleBinary(d / "Empty.vst3").has_value());

    fs::remove_all(d, ec);
    return true;
}

// after SoundCraft (crates/vst3-host/src/scan.rs tests::os_paths_cover_the_standard_locations)
bool testOsPathsCoverTheStandardLocations() {
    const auto mac = Vst3Host::osSearchPaths(Vst3Host::Os::MacOs, fs::u8path("/Users/u"), std::nullopt, std::nullopt);
    EXPECT_EQ(mac.size(), size_t(2));
    EXPECT_EQ(mac[0], fs::u8path("/Users/u/Library/Audio/Plug-Ins/VST3"));
    EXPECT_EQ(mac[1], fs::u8path("/Library/Audio/Plug-Ins/VST3"));

    const auto win = Vst3Host::osSearchPaths(Vst3Host::Os::Windows, std::nullopt, fs::u8path("C:/CPF"), fs::u8path("C:/LAD"));
    EXPECT_EQ(win.size(), size_t(2));
    EXPECT_EQ(win[0], fs::u8path("C:/CPF") / "VST3");
    EXPECT_EQ(win[1], fs::u8path("C:/LAD") / "Programs" / "Common" / "VST3");

    const auto unixPaths = Vst3Host::osSearchPaths(Vst3Host::Os::Unix, fs::u8path("/home/u"), std::nullopt, std::nullopt);
    EXPECT_EQ(unixPaths.size(), size_t(3));
    EXPECT_EQ(unixPaths[0], fs::u8path("/home/u/.vst3"));
    EXPECT_EQ(unixPaths[1], fs::u8path("/usr/lib/vst3"));
    EXPECT_EQ(unixPaths[2], fs::u8path("/usr/local/lib/vst3"));

    EXPECT_TRUE(!Vst3Host::defaultSearchPaths().empty());
    return true;
}

// after SoundCraft (crates/vst3-host/src/scan.rs tests::sub_categories_map_to_categories)
bool testSubCategoriesMapToCategories() {
    auto c = [](const std::string& s) {
        return Vst3Host::categoryFromSubCategories(Vst3Host::splitSubCategories(s));
    };
    EXPECT_EQ(c("Fx|Reverb"), Dsp::Category::Reverb);
    EXPECT_EQ(c("Instrument|Synth"), Dsp::Category::Instrument);
    EXPECT_EQ(c("Fx|Dynamics"), Dsp::Category::Dynamics);
    EXPECT_EQ(c("Fx|EQ"), Dsp::Category::Eq);
    EXPECT_EQ(c("Fx|Filter"), Dsp::Category::Eq);
    EXPECT_EQ(c("Fx|Modulation"), Dsp::Category::Modulation);
    EXPECT_EQ(c("Fx|Distortion"), Dsp::Category::Harmonic);
    EXPECT_EQ(c("Fx|Pitch Shift"), Dsp::Category::PitchShift);
    EXPECT_EQ(c("Fx|Delay"), Dsp::Category::Delay);
    EXPECT_EQ(c("Fx"), Dsp::Category::Other);
    EXPECT_EQ(c(""), Dsp::Category::Other);

    const auto split = Vst3Host::splitSubCategories(" Fx || Delay |");
    EXPECT_EQ(split.size(), size_t(2));
    EXPECT_EQ(split[0], "Fx");
    EXPECT_EQ(split[1], "Delay");

    EXPECT_TRUE(Vst3Host::isInstrument(Vst3Host::splitSubCategories("Instrument|Sampler")));
    EXPECT_TRUE(!Vst3Host::isInstrument(Vst3Host::splitSubCategories("Fx|Instrument")));
    return true;
}

} // namespace

void registerVst3ScanTests() {
    Test::add("vst3_finds_files_and_bundles_recursively_but_not_inside_bundles", testFindsFilesAndBundlesRecursivelyButNotInsideBundles);
    Test::add("vst3_bundles_resolve_per_os_layout", testBundlesResolvePerOsLayout);
    Test::add("vst3_os_paths_cover_the_standard_locations", testOsPathsCoverTheStandardLocations);
    Test::add("vst3_sub_categories_map_to_categories", testSubCategoriesMapToCategories);
}
