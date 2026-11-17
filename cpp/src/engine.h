// Minimal command engine for selftests (after SoundCraft crates/engine).
#pragma once

#include "session.h"
#include "time_range.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ImportAudio {
struct DecodedImport;
}

namespace EngCmd {

struct Result {
    bool ok = false;
    std::string error;
    std::map<std::string, double> nums;
    std::map<std::string, std::string> strs;
    std::vector<double> arr;
    static Result err(std::string e);
    static Result num(const std::string& k, double v);
};

struct Clipboard {
    long long length = 0;
    std::vector<std::vector<Session::Clip>> tracks;
    std::vector<std::vector<Session::AutomationLane>> automation;
    std::vector<std::vector<std::pair<long long, float>>> clipGain;
    std::vector<std::pair<std::string, double>> clipEffects;
    std::vector<Session::ClipboardMarker> markers;
    long long gridOffset = 0;
    std::optional<long long> cutFrom;
    bool automationOnly = false;
};

struct LastSelection {
    std::vector<uint64_t> tracks;
    Time::Range range;
};

class Engine {
public:
    // JSON-like command parameters for execute().
    struct Params {
        std::map<std::string, double> nums;
        std::map<std::string, std::string> strs;
        std::map<std::string, bool> bools;
        std::map<std::string, std::vector<uint64_t>> idLists;
        std::map<std::string, std::vector<double>> numLists;
        std::map<std::string, std::vector<std::string>> strLists;
        std::map<std::string, std::map<std::string, double>> objects;

        bool has(const char* k) const;
        std::optional<double> num(const char* k) const;
        std::optional<int64_t> i64(const char* k) const;
        std::optional<uint64_t> u64(const char* k) const;
        std::optional<std::string> str(const char* k) const;
        std::optional<bool> boolean(const char* k) const;
        const std::vector<uint64_t>* ids(const char* k) const;
        double numOr(const char* k, double def) const;
    };

    Session::Document doc;
    Clipboard clipboard;
    uint32_t revision = 0;
    uint32_t touch = 0;

    static Engine demo();
    static Engine fresh();

    const Session::Document& session() const { return doc; }
    Session::Document& sessionMut() { return doc; }

    size_t undoLen() const { return undoStack_.size(); }
    bool canUndo() const { return !undoStack_.empty(); }
    bool undo();
    bool redo();
    std::optional<std::string> undoLabel() const;

    bool commandEnabled(const char* cmd) const;
    Result execute(const char* cmd, const Params& p = {});

    // after SoundCraft (crates/engine/src/lib.rs offer_import, take_import, drop_offered_imports)
    void offerImport(ImportAudio::DecodedImport d);
    std::optional<ImportAudio::DecodedImport> takeImport(const std::filesystem::path& path);
    void dropOfferedImports();

    Session::Document roundTripSession() const;

private:
    std::vector<Session::Document> undoStack_;
    std::vector<Session::Document> redoStack_;
    std::vector<std::string> undoLabels_;
    std::vector<std::string> redoLabels_;
    std::optional<LastSelection> lastSelection_;
    std::string lastUndoLabel_;
    std::vector<std::shared_ptr<ImportAudio::DecodedImport>> imports_;

    void pushUndo(const Session::Document& snapshot, const char* label);
    void rememberSelection(const Session::Document& before);
    Result dispatch(const char* cmd, const Params& p);
};

std::vector<std::vector<float>> renderClips(const Session::Document& doc, uint64_t trackNum, Time::Range range);

} // namespace EngCmd

using Engine = EngCmd::Engine;
using EngResult = EngCmd::Result;
using EngParams = EngCmd::Engine::Params;
using EngCmd::renderClips;