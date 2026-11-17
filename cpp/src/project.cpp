// Polyhymnia - line-oriented session file I/O (after SoundCraft session save; JSON-like keys).
#include "project.h"
#include "import_audio.h"
#include "mix.h"

#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace Project {

static std::string shapeId(Session::FadeShape s) {
    switch (s) {
    case Session::FadeShape::Linear: return "linear";
    case Session::FadeShape::EqualPower: return "equal";
    case Session::FadeShape::SCurve: return "s";
    case Session::FadeShape::Exponential: return "exp";
    case Session::FadeShape::Logarithmic: return "log";
    }
    return "equal";
}

static bool parseShape(const std::string& s, Session::FadeShape& out) {
    if (s == "linear") { out = Session::FadeShape::Linear; return true; }
    if (s == "equal") { out = Session::FadeShape::EqualPower; return true; }
    if (s == "s") { out = Session::FadeShape::SCurve; return true; }
    if (s == "exp") { out = Session::FadeShape::Exponential; return true; }
    if (s == "log") { out = Session::FadeShape::Logarithmic; return true; }
    return false;
}

static void trim(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i) s.erase(0, i);
}

static std::string relPath(const fs::path& projectDir, const std::string& stored) {
    fs::path p = fs::u8path(stored);
    if (p.is_absolute()) {
        std::error_code ec;
        fs::path rel = fs::relative(p, projectDir, ec);
        if (!ec) return rel.u8string();
    }
    return stored;
}

bool save(const Session::Document& doc, const std::string& path, std::string& err) {
    std::ofstream f(fs::u8path(path), std::ios::binary);
    if (!f) { err = "Could not write the project file."; return false; }
    const fs::path folder = fs::u8path(path).parent_path();
    f << "polyhymnia " << Mix::kProjectFormatVersion << "\n";
    f << "session.name=" << doc.name << "\n";
    f << "session.rate=" << doc.sampleRate << "\n";
    f << "session.tempo=" << doc.time.tempoBpm << "\n";
    f << "session.meter=" << doc.time.meter.num << "/" << doc.time.meter.den << "\n";
    for (const auto& m : doc.markers) {
        f << "marker." << m.id << ".name=" << m.name << "\n";
        f << "marker." << m.id << ".at=" << m.start << "\n";
    }
    for (const auto& s : doc.sources) {
        std::string savedPath = relPath(folder, s.path);
        if (s.unsaved && s.numId != 0 && !folder.empty()) {
            if (auto audio = doc.pool.get(s.numId)) {
                std::error_code ec;
                const fs::path relTarget = fs::u8path(savedPath).is_relative() ? (folder / fs::u8path(savedPath))
                                                                                : (folder / "Audio Files" / (s.name.empty() ? (s.id + ".wav") : (s.name + ".wav")));
                fs::create_directories(relTarget.parent_path(), ec);
                const auto wavBytes = ImportAudio::encodeWav(audio->buffer, 32);
                std::ofstream wf(relTarget, std::ios::binary);
                if (wf) {
                    wf.write(reinterpret_cast<const char*>(wavBytes.data()), static_cast<std::streamsize>(wavBytes.size()));
                    savedPath = relPath(folder, relTarget.u8string());
                }
            }
        }
        f << "source." << s.id << ".num_id=" << s.numId << "\n";
        f << "source." << s.id << ".name=" << s.name << "\n";
        f << "source." << s.id << ".path=" << savedPath << "\n";
        f << "source." << s.id << ".rate=" << s.sampleRate << "\n";
        f << "source." << s.id << ".channels=" << s.channels << "\n";
        f << "source." << s.id << ".frames=" << s.frames << "\n";
        f << "source." << s.id << ".format=" << Session::fileFormatName(s.format) << "\n";
    }
    for (const auto& t : doc.tracks) {
        f << "track." << t.id << ".num_id=" << t.numId << "\n";
        f << "track." << t.id << ".name=" << t.name << "\n";
        f << "track." << t.id << ".kind=" << Session::trackKindId(t.kind) << "\n";
        f << "track." << t.id << ".format=" << Session::channelFormatLabel(t.format) << "\n";
        f << "track." << t.id << ".mute=" << (t.mute ? 1 : 0) << "\n";
        f << "track." << t.id << ".solo=" << (t.solo ? 1 : 0) << "\n";
        f << "track." << t.id << ".gain_db=" << t.gainDb << "\n";
        f << "track." << t.id << ".pan=" << t.pan << "\n";
        for (const auto& c : t.clips) {
            const std::string p = "clip." + t.id + "." + c.id;
            f << p << ".num_id=" << c.numId << "\n";
            f << p << ".name=" << c.name << "\n";
            f << p << ".source=" << c.sourceId << "\n";
            f << p << ".start=" << c.start << "\n";
            f << p << ".offset=" << c.offset << "\n";
            f << p << ".length=" << c.length << "\n";
            f << p << ".gain_db=" << c.gainDb << "\n";
            f << p << ".fade_in=" << c.fadeIn.len << "," << shapeId(c.fadeIn.shape) << "," << c.fadeIn.curvature << "\n";
            f << p << ".fade_out=" << c.fadeOut.len << "," << shapeId(c.fadeOut.shape) << "," << c.fadeOut.curvature << "\n";
        }
    }
    return true;
}

static bool parseLong(const std::string& v, long long& out) {
    try {
        out = std::stoll(v);
        return true;
    } catch (...) { return false; }
}

bool load(const std::string& path, Session::Document& doc, std::string& err) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) { err = "Could not open the project file."; return false; }
    doc = Session::empty();
    doc.folder = fs::u8path(path).parent_path().u8string();
    std::string line;
    if (!std::getline(f, line) || line.find("polyhymnia") != 0) { err = "Not a Polyhymnia project."; return false; }
    {
        const size_t sp = line.find(' ');
        if (sp != std::string::npos) doc.version = std::stoi(line.substr(sp + 1));
    }
    Session::Track* curTrack = nullptr;
    Session::Clip curClip;
    bool inClip = false;
    while (std::getline(f, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "session.name") doc.name = val;
        else if (key == "session.rate") doc.sampleRate = std::stod(val);
        else if (key == "session.tempo") doc.time.tempoBpm = std::stod(val);
        else if (key == "session.meter") {
            const size_t slash = val.find('/');
            if (slash != std::string::npos) {
                doc.time.meter.num = std::stoi(val.substr(0, slash));
                doc.time.meter.den = std::stoi(val.substr(slash + 1));
            }
        } else if (key.rfind("marker.", 0) == 0) {
            const size_t dot = key.find('.', 7);
            if (dot == std::string::npos) continue;
            const std::string id = key.substr(7, dot - 7);
            const std::string field = key.substr(dot + 1);
            auto* m = static_cast<Session::Marker*>(nullptr);
            for (auto& mk : doc.markers)
                if (mk.id == id) { m = &mk; break; }
            if (!m) {
                doc.markers.push_back({id, "", 0, 0, 1, 0});
                m = &doc.markers.back();
            }
            if (field == "name") m->name = val;
            else if (field == "at") parseLong(val, m->start);
        } else if (key.rfind("source.", 0) == 0) {
            const size_t dot = key.find('.', 7);
            if (dot == std::string::npos) continue;
            const std::string id = key.substr(7, dot - 7);
            const std::string field = key.substr(dot + 1);
            Session::Source* s = Session::findSource(doc, id);
            if (!s) {
                Session::Source ns;
                ns.id = id;
                ns.numId = doc.nextSourceId++;
                ns.sampleRate = 48000;
                ns.channels = 2;
                doc.sources.push_back(ns);
                s = &doc.sources.back();
            }
            if (field == "num_id") {
                long long v = 0;
                if (parseLong(val, v) && v > 0) {
                    s->numId = static_cast<uint64_t>(v);
                    doc.nextSourceId = std::max(doc.nextSourceId, s->numId + 1);
                }
            } else if (field == "name") s->name = val;
            else if (field == "path") s->path = val;
            else if (field == "rate") s->sampleRate = std::stoi(val);
            else if (field == "channels") s->channels = std::stoi(val);
            else if (field == "frames") parseLong(val, s->frames);
            else if (field == "format") {
                if (auto fmt = Session::fileFormatFromName(val)) s->format = *fmt;
            }
        } else if (key.rfind("track.", 0) == 0) {
            const size_t dot = key.find('.', 6);
            if (dot == std::string::npos) continue;
            const std::string id = key.substr(6, dot - 6);
            const std::string field = key.substr(dot + 1);
            curTrack = Session::findTrack(doc, id);
            if (!curTrack) {
                Session::Track tr;
                tr.id = id;
                tr.numId = doc.nextTrackId++;
                tr.name = id;
                tr.color = Session::nextTrackColor(doc);
                doc.tracks.push_back(std::move(tr));
                curTrack = &doc.tracks.back();
            }
            if (field == "num_id") {
                long long v = 0;
                if (parseLong(val, v) && v > 0) {
                    curTrack->numId = static_cast<uint64_t>(v);
                    doc.nextTrackId = std::max(doc.nextTrackId, curTrack->numId + 1);
                }
            } else if (field == "name") curTrack->name = val;
            else if (field == "kind") {
                if (auto k = Session::trackKindFromId(val)) curTrack->kind = *k;
            } else if (field == "format") {
                if (auto fmt = Session::channelFormatFromId(val)) curTrack->format = *fmt;
            } else if (field == "mute") curTrack->mute = val != "0";
            else if (field == "solo") curTrack->solo = val != "0";
            else if (field == "gain_db") curTrack->gainDb = (float)std::stod(val);
            else if (field == "pan") curTrack->pan = (float)std::stod(val);
        } else if (key.rfind("clip.", 0) == 0) {
            const size_t d1 = key.find('.', 5);
            const size_t d2 = key.find('.', d1 + 1);
            if (d1 == std::string::npos || d2 == std::string::npos) continue;
            const std::string tid = key.substr(5, d1 - 5);
            const std::string cid = key.substr(d1 + 1, d2 - d1 - 1);
            const std::string field = key.substr(d2 + 1);
            curTrack = Session::findTrack(doc, tid);
            if (!curTrack) continue;
            if (!inClip || curClip.id != cid) {
                if (inClip) curTrack->clips.push_back(curClip);
                curClip = Session::Clip{};
                curClip.id = cid;
                curClip.numId = Session::newClipNumId(doc);
                inClip = true;
            }
            if (field == "num_id") {
                long long v = 0;
                if (parseLong(val, v) && v > 0) {
                    curClip.numId = static_cast<uint64_t>(v);
                    doc.nextClipId = std::max(doc.nextClipId, curClip.numId + 1);
                }
            } else if (field == "name") curClip.name = val;
            else if (field == "source") curClip.sourceId = val;
            else if (field == "start") parseLong(val, curClip.start);
            else if (field == "offset") parseLong(val, curClip.offset);
            else if (field == "length") parseLong(val, curClip.length);
            else if (field == "gain_db") curClip.gainDb = (float)std::stod(val);
            else if (field == "fade_in" || field == "fade_out") {
                const size_t c1 = val.find(',');
                Session::Fade* fd = field == "fade_in" ? &curClip.fadeIn : &curClip.fadeOut;
                if (c1 != std::string::npos) {
                    parseLong(val.substr(0, c1), fd->len);
                    const size_t c2 = val.find(',', c1 + 1);
                    if (c2 != std::string::npos) {
                        parseShape(val.substr(c1 + 1, c2 - c1 - 1), fd->shape);
                        fd->curvature = static_cast<float>(std::stod(val.substr(c2 + 1)));
                    } else {
                        parseShape(val.substr(c1 + 1), fd->shape);
                    }
                }
            }
        }
    }
    if (inClip && curTrack) curTrack->clips.push_back(curClip);
    doc.touch++;
    return true;
}

}