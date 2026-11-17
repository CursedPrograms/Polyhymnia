// Polyhymnia - mix clips to stereo (after SoundCraft crates/mix); no allocations in render.
#pragma once
#include "session.h"

#include <vector>

namespace Mix {
struct Buffer {
    std::vector<float> mono;
    int sampleRate = 48000;
};
bool ensureSource(const Session::Document& doc, const std::string& sourceId, Buffer& buf);
void render(const Session::Document& doc, long long startSample, float* outStereo, unsigned frames, double sampleRate);
long long sessionLength(const Session::Document& doc);
bool renderToWav(const Session::Document& doc, const std::string& path, std::string& err);
}