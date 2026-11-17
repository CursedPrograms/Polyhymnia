// Polyhymnia - realtime/offline mix of session clips with gain, pan, mute and solo.
#include "mix.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace Mix {

static std::unordered_map<std::string, Buffer> cache;

static float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

static float fadeGain(const Session::Fade& f, long long posInClip, bool fadeIn) {
    if (f.len <= 0) return 1.0f;
    long long p = fadeIn ? posInClip : (f.len - posInClip);
    if (p < 0) p = 0;
    if (p >= f.len) return fadeIn ? 1.0f : 0.0f;
    const float t = (float)p / (float)f.len;
    switch (f.shape) {
    case Session::FadeShape::Linear: return fadeIn ? t : 1.0f - t;
    case Session::FadeShape::EqualPower: return fadeIn ? std::sin(t * 1.5707963f) : std::cos(t * 1.5707963f);
    default: return fadeIn ? t : 1.0f - t;
    }
}

// Built-in 440 Hz tone for missing demo WAV files (after SoundCraft demo synthesis idea).
static void fillSine(Buffer& b, double seconds, double hz = 440.0) {
    const int n = (int)(b.sampleRate * seconds);
    b.mono.resize((size_t)n);
    for (int i = 0; i < n; ++i)
        b.mono[(size_t)i] = 0.25f * (float)std::sin(2.0 * 3.141592653589793 * hz * i / b.sampleRate);
}

static void fillClick(Buffer& b) {
    const int n = b.sampleRate / 10;
    b.mono.assign((size_t)n, 0.0f);
    for (int i = 0; i < n / 20; ++i) b.mono[(size_t)i] = 0.5f * (1.0f - (float)i / (float)(n / 20));
}

bool ensureSource(const Session::Document& doc, const std::string& sourceId, Buffer& buf) {
    const auto it = cache.find(sourceId);
    if (it != cache.end()) {
        buf = it->second;
        return true;
    }
    const Session::Source* src = nullptr;
    for (const auto& s : doc.sources)
        if (s.id == sourceId) { src = &s; break; }
    if (!src) return false;
    Buffer fresh;
    fresh.sampleRate = src->sampleRate > 0 ? src->sampleRate : 48000;
    if (src->path.find("click") != std::string::npos) fillClick(fresh);
    else fillSine(fresh, 4.0);
    cache[sourceId] = fresh;
    buf = fresh;
    return true;
}

void render(const Session::Document& doc, long long startSample, float* outStereo, unsigned frames, double sampleRate) {
    std::memset(outStereo, 0, frames * 2 * sizeof(float));
    bool anySolo = false;
    for (const auto& t : doc.tracks)
        if (t.solo) { anySolo = true; break; }
    for (const auto& track : doc.tracks) {
        if (track.mute) continue;
        if (anySolo && !track.solo) continue;
        const float trackGain = dbToLin(track.gainDb);
        const float pan = std::clamp(track.pan, -1.0f, 1.0f);
        const float lPan = std::cos((pan + 1.0f) * 0.25f * 3.14159265f);
        const float rPan = std::sin((pan + 1.0f) * 0.25f * 3.14159265f);
        for (const auto& clip : track.clips) {
            Buffer srcBuf;
            if (!ensureSource(doc, clip.sourceId, srcBuf) || srcBuf.mono.empty()) continue;
            const double ratio = sampleRate / (double)srcBuf.sampleRate;
            for (unsigned i = 0; i < frames; ++i) {
                const long long timeline = startSample + (long long)i;
                if (timeline < clip.start || timeline >= clip.start + clip.length) continue;
                const long long inClip = timeline - clip.start;
                const long long srcPos = clip.offset + (long long)(inClip / ratio);
                if (srcPos < 0 || srcPos >= (long long)srcBuf.mono.size()) continue;
                float s = srcBuf.mono[(size_t)srcPos];
                s *= dbToLin(clip.gainDb) * trackGain;
                s *= fadeGain(clip.fadeIn, inClip, true);
                if (clip.fadeOut.len > 0 && inClip >= clip.length - clip.fadeOut.len) {
                    const long long tail = inClip - (clip.length - clip.fadeOut.len);
                    s *= 1.0f - fadeGain(clip.fadeOut, tail, true);
                }
                outStereo[i * 2] += s * lPan;
                outStereo[i * 2 + 1] += s * rPan;
            }
        }
    }
}

long long sessionLength(const Session::Document& doc) {
    long long end = 0;
    for (const auto& t : doc.tracks)
        for (const auto& c : t.clips)
            end = std::max(end, c.start + c.length);
    return end > 0 ? end : doc.sampleRate > 0 ? (long long)doc.sampleRate : 48000;
}

bool renderToWav(const Session::Document& doc, const std::string& path, std::string& err) {
    const long long total = sessionLength(doc);
    const int rate = (int)doc.sampleRate;
    if (total <= 0 || rate <= 0) { err = "Nothing to render."; return false; }
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "wb");
#else
    f = fopen(path.c_str(), "wb");
#endif
    if (!f) { err = "Could not write WAV."; return false; }
    const int channels = 2;
    const int bits = 16;
    const int byteRate = rate * channels * bits / 8;
    const int dataBytes = (int)(total * channels * (bits / 8));
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    w32((uint32_t)(36 + dataBytes));
    fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(1);
    w16((uint16_t)channels);
    w32((uint32_t)rate);
    w32((uint32_t)byteRate);
    w16((uint16_t)(channels * bits / 8));
    w16((uint16_t)bits);
    fwrite("data", 1, 4, f);
    w32((uint32_t)dataBytes);
    std::vector<float> block(4096 * 2);
    for (long long pos = 0; pos < total;) {
        const unsigned want = (unsigned)std::min<long long>(4096, total - pos);
        render(doc, pos, block.data(), want, rate);
        for (unsigned i = 0; i < want * 2; ++i) {
            float s = std::clamp(block[i], -1.0f, 1.0f);
            const int16_t v = (int16_t)(s * 32767.0f);
            fwrite(&v, 2, 1, f);
        }
        pos += want;
    }
    fclose(f);
    return true;
}

}