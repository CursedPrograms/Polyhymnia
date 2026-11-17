// after SoundCraft (crates/engine/src/io/render.rs consolidate)
#include "consolidate.h"
#include "clip_ops.h"
#include "engine.h"
#include "engine_edit.h"
#include "mix.h"

#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace Consolidate {

namespace {

constexpr size_t kMaxRenderSamples = size_t{1} << 29;

std::string safeBaseName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char ch : name) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|')
            out.push_back('_');
        else
            out.push_back(ch);
    }
    if (out.empty()) return "track";
    return out;
}

std::string uniqueWavPath(const fs::path& folder, const std::string& base) {
    const std::string stem = safeBaseName(base) + "_consolidated";
    fs::path p = folder / (stem + ".wav");
    for (int i = 1; fs::exists(p); ++i) p = folder / (stem + "_" + std::to_string(i) + ".wav");
    return p.u8string();
}

} // namespace

bool track(Session::Document& doc, const std::string& trackId, Time::Range range, std::string& err) {
    Session::Track* tr = Session::findTrack(doc, trackId);
    if (!tr || !Session::hasPlaylist(tr->kind) || Session::isMidiKind(tr->kind)) return false;
    const long long start = std::max(0LL, range.start);
    const long long len = range.end - start;
    if (len <= 0) return false;
    const size_t maxFrames = kMaxRenderSamples;
    if (static_cast<size_t>(len) > maxFrames) {
        std::ostringstream os;
        os.setf(std::ios::fixed);
        os.precision(1);
        os << (static_cast<double>(maxFrames) / doc.sampleRate) << " s";
        err = "selection is too long to consolidate on `" + tr->name + "` (at most " + os.str() + ")";
        return false;
    }
    const Time::Range r = Time::Range::newRange(start, start + len);
    const auto audio = EngCmd::renderClips(doc, tr->numId, r);
    if (audio.empty() || audio[0].size() != static_cast<size_t>(len)) {
        err = "consolidate rendered the wrong length";
        return false;
    }
    const std::vector<float>& pcm = audio[0];
    fs::path folder = doc.folder.empty() ? fs::temp_directory_path() : fs::u8path(doc.folder);
    std::error_code ec;
    fs::create_directories(folder, ec);
    const std::string wavPath = uniqueWavPath(folder, tr->name);
    if (!Mix::writeMonoPcmWav(wavPath, pcm.data(), static_cast<int>(len), static_cast<int>(doc.sampleRate), err)) return false;

    std::string storedPath = wavPath;
    if (!doc.folder.empty()) {
        fs::path rel = fs::relative(fs::u8path(wavPath), fs::u8path(doc.folder), ec);
        if (!ec) storedPath = rel.u8string();
    }

    Session::Source src;
    src.numId = doc.nextSourceId++;
    src.id = "src_" + std::to_string(src.numId);
    src.path = storedPath;
    src.sampleRate = static_cast<int>(doc.sampleRate);
    src.channels = 1;
    src.frames = len;
    src.monoPcm = pcm;
    doc.sources.push_back(std::move(src));
    const std::string& sourceId = doc.sources.back().id;

    EngEdit::clearRange(doc, trackId, r, false);
    Session::Clip clip;
    clip.numId = Session::newClipNumId(doc);
    clip.id = Session::newClipId(doc);
    clip.name = tr->name + "_consolidated";
    clip.sourceId = sourceId;
    clip.start = r.start;
    clip.offset = 0;
    clip.length = len;
    EngEdit::placeClip(doc, trackId, std::move(clip));
    return true;
}

} // namespace Consolidate