// Polyhymnia - in-memory session; see session.h.
#include "session.h"

namespace Session {

Document empty() {
    Document d;
    d.name = "Untitled";
    return d;
}

// A tiny two-track demo with markers for screenshots and round-trip save tests.
Document demo() {
    Document d;
    d.name = "Demo";
    d.time.tempoBpm = 120.0;
    d.time.meter = {4, 4};
    d.sources.push_back({"sine", "audio/sine_440.wav", 48000, 1});
    d.sources.push_back({"click", "audio/click.wav", 48000, 1});
    Track a;
    a.id = "t1";
    a.name = "Lead";
    a.clips.push_back({"c1", "sine", 0, 0, 48000 * 2, -3.0f, {}, {}});
    d.tracks.push_back(a);
    Track b;
    b.id = "t2";
    b.name = "Click";
    b.gainDb = -6.0f;
    b.clips.push_back({"c2", "click", 48000, 0, 48000, 0.0f, {}, {}});
    d.tracks.push_back(b);
    d.markers.push_back({"m1", "Intro", 0});
    d.markers.push_back({"m2", "Verse", 48000});
    return d;
}

Track* findTrack(Document& doc, const std::string& id) {
    for (auto& t : doc.tracks)
        if (t.id == id) return &t;
    return nullptr;
}

const Track* findTrack(const Document& doc, const std::string& id) {
    for (const auto& t : doc.tracks)
        if (t.id == id) return &t;
    return nullptr;
}

Source* findSource(Document& doc, const std::string& id) {
    for (auto& s : doc.sources)
        if (s.id == id) return &s;
    return nullptr;
}

}