// Polyhymnia - playhead on atomics; the UI sets flags, the callback advances and mixes.
#include "playback.h"
#include "mix.h"

#include <atomic>
#include <algorithm>

namespace Playback {

static std::atomic<bool> playing{false};
static std::atomic<bool> loopOn{false};
static std::atomic<long long> head{0};
static const Session::Document* session = nullptr;
static long long loopEnd = 48000 * 8;

void setDocument(const Session::Document* doc) {
    session = doc;
    if (doc) {
        long long end = 0;
        for (const auto& t : doc->tracks)
            for (const auto& c : t.clips)
                end = std::max(end, c.start + c.length);
        if (end > 0) loopEnd = end;
    }
}

void play() { playing.store(true); }
void stop() {
    playing.store(false);
    head.store(0);
}
void toggleLoop() { loopOn.store(!loopOn.load()); }
bool isPlaying() { return playing.load(); }
bool loopEnabled() { return loopOn.load(); }
long long playhead() { return head.load(); }
void setPlayhead(long long sample) { head.store(sample); }

void fillOutput(float* stereo, unsigned frames, double sampleRate) {
    if (!session || !playing.load()) {
        for (unsigned i = 0; i < frames * 2; ++i) stereo[i] = 0.0f;
        return;
    }
    const long long pos = head.load();
    Mix::render(*session, pos, stereo, frames, sampleRate);
    long long next = pos + (long long)frames;
    if (loopOn.load() && next >= loopEnd) next = 0;
    head.store(next);
}

void advanceUi() {}

}