// Polyhymnia - line-oriented session file I/O (after SoundCraft session save; JSON-like keys).
#include "project.h"

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

bool save(const Session::Document& doc, const std::string& path, std::string& err) {
    std::ofstream f(fs::u8path(path), std::ios::binary);
    if (!f) { err = "Could not write the project file."; return false; }
    f << "polyhymnia " << doc.version << "\n";
    f << "session.name=" << doc.name << "\n";
    f << "session.rate=" << doc.sampleRate << "\n";
    f << "session.tempo=" << doc.time.tempoBpm << "\n";
    f << "session.meter=" << doc.time.meter.num << "/" << doc.time.meter.den << "\n";
    for (const auto& m : doc.markers) {
        f << "marker." << m.id << ".name=" << m.name << "\n";
        f << "marker." << m.id << ".at=" << m.at << "\n";
    }
    for (const auto& s : doc.sources) {
        f << "source." << s.id << ".path=" << s.path << "\n";
        f << "source." << s.id << ".rate=" << s.sampleRate << "\n";
        f << "source." << s.id << ".channels=" << s.channels << "\n";
    }
    for (const auto& t : doc.tracks) {
        f << "track." << t.id << ".name=" << t.name << "\n";
        f << "track." << t.id << ".mute=" << (t.mute ? 1 : 0) << "\n";
        f << "track." << t.id << ".solo=" << (t.solo ? 1 : 0) << "\n";
        f << "track." << t.id << ".gain_db=" << t.gainDb << "\n";
        f << "track." << t.id << ".pan=" << t.pan << "\n";
        for (const auto& c : t.clips) {
            const std::string p = "clip." + t.id + "." + c.id;
            f << p << ".source=" << c.sourceId << "\n";
            f << p << ".start=" << c.start << "\n";
            f << p << ".offset=" << c.offset << "\n";
            f << p << ".length=" << c.length << "\n";
            f << p << ".gain_db=" << c.gainDb << "\n";
            f << p << ".fade_in=" << c.fadeIn.len << "," << shapeId(c.fadeIn.shape) << "\n";
            f << p << ".fade_out=" << c.fadeOut.len << "," << shapeId(c.fadeOut.shape) << "\n";
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
            if (!m) { doc.markers.push_back({id, "", 0}); m = &doc.markers.back(); }
            if (field == "name") m->name = val;
            else if (field == "at") parseLong(val, m->at);
        } else if (key.rfind("source.", 0) == 0) {
            const size_t dot = key.find('.', 7);
            if (dot == std::string::npos) continue;
            const std::string id = key.substr(7, dot - 7);
            const std::string field = key.substr(dot + 1);
            Session::Source* s = Session::findSource(doc, id);
            if (!s) { doc.sources.push_back({id, "", 48000, 2}); s = &doc.sources.back(); }
            if (field == "path") s->path = val;
            else if (field == "rate") s->sampleRate = std::stoi(val);
            else if (field == "channels") s->channels = std::stoi(val);
        } else if (key.rfind("track.", 0) == 0) {
            const size_t dot = key.find('.', 6);
            if (dot == std::string::npos) continue;
            const std::string id = key.substr(6, dot - 6);
            const std::string field = key.substr(dot + 1);
            curTrack = Session::findTrack(doc, id);
            if (!curTrack) {
                doc.tracks.push_back({id, id, false, false, 0, 0, {}});
                curTrack = &doc.tracks.back();
            }
            if (field == "name") curTrack->name = val;
            else if (field == "mute") curTrack->mute = val != "0";
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
                inClip = true;
            }
            if (field == "source") curClip.sourceId = val;
            else if (field == "start") parseLong(val, curClip.start);
            else if (field == "offset") parseLong(val, curClip.offset);
            else if (field == "length") parseLong(val, curClip.length);
            else if (field == "gain_db") curClip.gainDb = (float)std::stod(val);
            else if (field == "fade_in" || field == "fade_out") {
                const size_t comma = val.find(',');
                Session::Fade* fd = field == "fade_in" ? &curClip.fadeIn : &curClip.fadeOut;
                if (comma != std::string::npos) {
                    parseLong(val.substr(0, comma), fd->len);
                    parseShape(val.substr(comma + 1), fd->shape);
                }
            }
        }
    }
    if (inClip && curTrack) curTrack->clips.push_back(curClip);
    return true;
}

}