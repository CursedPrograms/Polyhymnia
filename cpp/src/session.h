// Polyhymnia - session document: tracks, clips, markers, sources (after SoundCraft crates/model).
#pragma once
#include "tempo.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Session {

enum class FadeShape { Linear, EqualPower, SCurve, Exponential, Logarithmic };

struct Fade {
    long long len = 0;
    FadeShape shape = FadeShape::EqualPower;
};

struct Clip {
    std::string id;
    std::string sourceId;
    long long start = 0;
    long long offset = 0;
    long long length = 0;
    float gainDb = 0.0f;
    Fade fadeIn;
    Fade fadeOut;
};

struct Track {
    std::string id;
    std::string name;
    bool mute = false;
    bool solo = false;
    float gainDb = 0.0f;
    float pan = 0.0f;
    std::vector<Clip> clips;
};

struct Marker {
    std::string id;
    std::string name;
    long long at = 0;
};

struct Source {
    std::string id;
    std::string path;
    int sampleRate = 48000;
    int channels = 2;
};

struct Document {
    int version = 1;
    uint32_t touch = 0;
    std::string name = "Untitled";
    double sampleRate = 48000.0;
    Time::Map time;
    std::vector<Source> sources;
    std::vector<Track> tracks;
    std::vector<Marker> markers;
    std::string folder;
};

Document empty();
Document demo();
Track* findTrack(Document& doc, const std::string& id);
const Track* findTrack(const Document& doc, const std::string& id);
Source* findSource(Document& doc, const std::string& id);
}