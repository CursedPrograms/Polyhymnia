// Polyhymnia - mix clips to stereo (after SoundCraft crates/mix); no allocations in the audio callback.
#pragma once
#include "session.h"

#include <memory>
#include <string>
#include <vector>

namespace Mix {

constexpr int kProjectFormatVersion = 1;

// Decoded PCM: stereo interleaved floats, one frame = two samples.
struct Buffer {
    std::vector<float> stereo;
    int sampleRate = 48000;
    int frames = 0;
    bool synthetic = false;
};

// Peak buckets for drawing waveforms (min/max per bucket).
struct Peaks {
    std::vector<float> minMax;
    int buckets = 0;
};

struct MixPlan {
    struct ClipPlay {
        Session::Clip clip;
        std::shared_ptr<const Buffer> audio;
        Peaks peaks;
    };
    struct TrackPlay {
        Session::Track track;
        std::vector<ClipPlay> clips;
    };
    std::vector<TrackPlay> tracks;
    long long length = 0;
    double sampleRate = 48000;
    std::string missingNote;
};

void clearPlan();
void rebuildPlan(const Session::Document& doc);
const std::shared_ptr<const MixPlan>& currentPlan();

void render(long long startSample, float* outStereo, unsigned frameCount, double sampleRate);
long long sessionLength(const Session::Document& doc);
bool renderToWav(const Session::Document& doc, const std::string& path, std::string& err);

bool writeTestToneWav(const std::string& path, double hz, double seconds, int sampleRate, std::string& err);
bool verifyToneInWav(const std::string& path, double expectHz, double sampleRate, std::string& err);

Peaks peaksForClip(const MixPlan::ClipPlay& clip, int bucketCount);
const Peaks& cachedPeaks(const std::string& sourceId, const Session::Clip& clip, int buckets);

} // namespace Mix