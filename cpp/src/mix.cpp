// Polyhymnia - realtime/offline mix with miniaudio decode (after SoundCraft crates/mix).
#include "mix.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Mix {

static std::shared_ptr<const MixPlan> livePlan;
static std::mutex peakMtx;
static std::unordered_map<std::string, Peaks> peakCache;
static std::unordered_map<std::string, Peaks> sourceLodPeaks;

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
    case Session::FadeShape::SCurve: {
        const float s = t * t * (3.0f - 2.0f * t);
        return fadeIn ? s : 1.0f - s;
    }
    case Session::FadeShape::Exponential: {
        const float k = (std::exp(4.0f) - 1.0f);
        const float u = (std::exp(t * 4.0f) - 1.0f) / k;
        return fadeIn ? u : 1.0f - u;
    }
    case Session::FadeShape::Logarithmic: {
        const float denom = std::log1p(std::exp(1.0f) - 1.0f);
        const float u = std::log1p(t * (std::exp(1.0f) - 1.0f)) / denom;
        return fadeIn ? u : 1.0f - u;
    }
    }
    return fadeIn ? t : 1.0f - t;
}

// Resample decoded PCM with miniaudio's resampler (anti-alias LP on the linear backend).
static bool resampleStereo(const std::vector<float>& in, int inRate, int inFrames, int outRate, std::vector<float>& out) {
    if (inRate == outRate || inFrames <= 0) {
        out = in;
        return true;
    }
    ma_resampler_config cfg = ma_resampler_config_init(ma_format_f32, 2, (ma_uint32)inRate, (ma_uint32)outRate, ma_resample_algorithm_linear);
    cfg.linear.lpfOrder = 4;
    ma_resampler resampler;
    if (ma_resampler_init(&cfg, nullptr, &resampler) != MA_SUCCESS) return false;
    ma_uint64 outCap = 0;
    if (ma_resampler_get_expected_output_frame_count(&resampler, (ma_uint64)inFrames, &outCap) != MA_SUCCESS) {
        ma_resampler_uninit(&resampler, nullptr);
        return false;
    }
    out.assign((size_t)outCap * 2, 0.0f);
    ma_uint64 inCount = (ma_uint64)inFrames;
    ma_uint64 outCount = outCap;
    const ma_result pr = ma_resampler_process_pcm_frames(&resampler, in.data(), &inCount, out.data(), &outCount);
    ma_resampler_uninit(&resampler, nullptr);
    if (pr != MA_SUCCESS) return false;
    out.resize((size_t)outCount * 2);
    return true;
}

static void fillSine(Buffer& b, double seconds, double hz) {
    const int n = (int)(b.sampleRate * seconds);
    b.frames = n;
    b.stereo.resize((size_t)n * 2);
    for (int i = 0; i < n; ++i) {
        const float s = 0.25f * (float)std::sin(2.0 * 3.141592653589793 * hz * i / b.sampleRate);
        b.stereo[(size_t)i * 2] = s;
        b.stereo[(size_t)i * 2 + 1] = s;
    }
    b.synthetic = true;
}

static void fillClick(Buffer& b) {
    const int n = b.sampleRate / 10;
    b.frames = n;
    b.stereo.assign((size_t)n * 2, 0.0f);
    for (int i = 0; i < n / 20; ++i) {
        const float v = 0.5f * (1.0f - (float)i / (float)(n / 20));
        b.stereo[(size_t)i * 2] = v;
        b.stereo[(size_t)i * 2 + 1] = v;
    }
    b.synthetic = true;
}

static fs::path resolveAudioPath(const Session::Document& doc, const std::string& relOrAbs) {
    fs::path p = fs::u8path(relOrAbs);
    if (p.is_relative() && !doc.folder.empty()) p = fs::u8path(doc.folder) / p;
    return p;
}

// Load one source off the audio/UI thread; uses in-memory SourcePool when available.
static std::shared_ptr<const Buffer> loadSource(const Session::Document& doc, const Session::Source& src, std::string& missing) {
    auto out = std::make_shared<Buffer>();
    out->sampleRate = (int)doc.sampleRate;
    if (src.numId != 0) {
        if (auto pooled = doc.pool.get(src.numId)) {
            const size_t nFrames = pooled->frames();
            const size_t nCh = pooled->buffer.channels.size();
            if (nFrames > 0 && nCh > 0) {
                out->frames = static_cast<int>(nFrames);
                out->stereo.resize(nFrames * 2, 0.0f);
                for (size_t i = 0; i < nFrames; ++i) {
                    const float l = pooled->buffer.channels[0][i];
                    const float r = (nCh >= 2) ? pooled->buffer.channels[1][i] : l;
                    out->stereo[i * 2] = l;
                    out->stereo[i * 2 + 1] = r;
                }
                out->synthetic = false;
                return out;
            }
        }
    }
    if (!src.monoPcm.empty()) {
        const size_t nFrames = src.monoPcm.size();
        out->frames = static_cast<int>(nFrames);
        out->stereo.resize(nFrames * 2, 0.0f);
        for (size_t i = 0; i < nFrames; ++i) {
            out->stereo[i * 2] = src.monoPcm[i];
            out->stereo[i * 2 + 1] = src.monoPcm[i];
        }
        out->synthetic = false;
        return out;
    }
    const fs::path path = resolveAudioPath(doc, src.path);
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        if (src.path.find("click") != std::string::npos) fillClick(*out);
        else fillSine(*out, 4.0, 440.0);
        if (missing.empty()) missing = "Missing audio: using placeholder tone for " + src.path;
        else missing += "; " + src.path;
        return out;
    }
    ma_decoder decoder;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_file(path.u8string().c_str(), &cfg, &decoder) != MA_SUCCESS) {
        fillSine(*out, 4.0, 440.0);
        if (missing.empty()) missing = "Could not decode: " + src.path;
        return out;
    }
    const int channels = (int)decoder.outputChannels;
    const int fileRate = (int)decoder.outputSampleRate;
    std::vector<float> raw;
    const ma_uint32 chunk = 4096;
    std::vector<float> chunkBuf(chunk * (ma_uint32)channels);
    ma_uint64 framesRead = 0;
    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    if (totalFrames > 0) raw.reserve((size_t)totalFrames * 2);
    for (;;) {
        ma_uint64 got = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&decoder, chunkBuf.data(), chunk, &got);
        if (r != MA_SUCCESS || got == 0) break;
        for (ma_uint64 i = 0; i < got; ++i) {
            if (channels == 1) {
                const float m = chunkBuf[(size_t)i];
                raw.push_back(m);
                raw.push_back(m);
            } else {
                raw.push_back(chunkBuf[(size_t)i * (size_t)channels]);
                raw.push_back(chunkBuf[(size_t)i * (size_t)channels + 1]);
            }
        }
        framesRead += got;
        if (totalFrames > 0 && framesRead >= totalFrames) break;
    }
    ma_decoder_uninit(&decoder);
    const int inFrames = (int)(raw.size() / 2);
    std::vector<float> stereo;
    if (!resampleStereo(raw, fileRate, inFrames, out->sampleRate, stereo)) {
        fillSine(*out, 4.0, 440.0);
        if (missing.empty()) missing = "Resample failed: " + src.path;
        return out;
    }
    out->frames = (int)(stereo.size() / 2);
    out->stereo = std::move(stereo);
    out->synthetic = false;
    return out;
}

// Min/max peaks over frame ranges (mono-ish mix of L/R).
static Peaks buildPeaksSpan(const Buffer& buf, int frameStart, int frameEnd, int buckets) {
    Peaks p;
    if (buckets <= 0 || frameEnd <= frameStart) return p;
    p.buckets = buckets;
    p.minMax.resize((size_t)buckets * 2, 0.0f);
    const int span = frameEnd - frameStart;
    const int per = std::max(1, span / buckets);
    for (int b = 0; b < buckets; ++b) {
        const int start = frameStart + b * per;
        const int end = std::min(frameEnd, start + per);
        float mn = 1.0f, mx = -1.0f;
        for (int i = start; i < end; ++i) {
            if (i < 0 || i >= buf.frames) continue;
            const float s = 0.5f * (buf.stereo[(size_t)i * 2] + buf.stereo[(size_t)i * 2 + 1]);
            mn = std::min(mn, s);
            mx = std::max(mx, s);
        }
        if (end <= start) {
            mn = 0;
            mx = 0;
        }
        p.minMax[(size_t)b * 2] = mn;
        p.minMax[(size_t)b * 2 + 1] = mx;
    }
    return p;
}

Peaks buildPeaks(const Buffer& buf, int buckets) {
    return buildPeaksSpan(buf, 0, buf.frames, buckets);
}

void clearPlan() { std::atomic_store(&livePlan, std::shared_ptr<const MixPlan>{}); }

void rebuildPlan(const Session::Document& doc) {
    auto plan = std::make_shared<MixPlan>();
    plan->sampleRate = doc.sampleRate;
    plan->length = sessionLength(doc);
    std::unordered_map<std::string, std::shared_ptr<const Buffer>> loaded;
    for (const auto& s : doc.sources) {
        loaded[s.id] = loadSource(doc, s, plan->missingNote);
        const auto& buf = loaded[s.id];
        if (buf && buf->frames > 0 && !buf->synthetic) {
            std::lock_guard<std::mutex> g(peakMtx);
            const std::string lodKey = s.id + ":lod4096";
            if (sourceLodPeaks.find(lodKey) == sourceLodPeaks.end()) sourceLodPeaks[lodKey] = buildPeaks(*buf, 4096);
        }
    }
    for (const auto& track : doc.tracks) {
        MixPlan::TrackPlay tp;
        tp.track = track;
        for (const auto& clip : track.clips) {
            MixPlan::ClipPlay cp;
            cp.clip = clip;
            auto it = loaded.find(clip.sourceId);
            cp.audio = it != loaded.end() ? it->second : std::make_shared<Buffer>();
            cp.peaks = buildPeaks(*cp.audio, 256);
            tp.clips.push_back(cp);
        }
        plan->tracks.push_back(tp);
    }
    std::atomic_store(&livePlan, std::shared_ptr<const MixPlan>(plan));
}

std::shared_ptr<const MixPlan> currentPlan() {
    return std::atomic_load(&livePlan);
}

void render(long long startSample, float* outStereo, unsigned frameCount, double sampleRate) {
    std::memset(outStereo, 0, frameCount * 2 * sizeof(float));
    const auto plan = std::atomic_load(&livePlan);
    if (!plan) return;
    bool anySolo = false;
    for (const auto& t : plan->tracks)
        if (t.track.solo) { anySolo = true; break; }
    for (const auto& tplay : plan->tracks) {
        const auto& track = tplay.track;
        if (track.mute) continue;
        if (anySolo && !track.solo) continue;
        const float trackGain = dbToLin(track.gainDb);
        const float pan = std::clamp(track.pan, -1.0f, 1.0f);
        const float lPan = std::cos((pan + 1.0f) * 0.25f * 3.14159265f);
        const float rPan = std::sin((pan + 1.0f) * 0.25f * 3.14159265f);
        for (const auto& cplay : tplay.clips) {
            const auto& clip = cplay.clip;
            const auto& src = cplay.audio;
            if (!src || src->frames <= 0) continue;
            const double ratio = sampleRate / (double)src->sampleRate;
            const float* pcm = src->stereo.data();
            for (unsigned i = 0; i < frameCount; ++i) {
                const long long timeline = startSample + (long long)i;
                if (timeline < clip.start || timeline >= clip.start + clip.length) continue;
                const long long inClip = timeline - clip.start;
                const long long srcPos = clip.offset + (long long)(inClip / ratio);
                if (srcPos < 0 || srcPos >= src->frames) continue;
                float l = pcm[(size_t)srcPos * 2];
                float r = pcm[(size_t)srcPos * 2 + 1];
                const float g = dbToLin(clip.gainDb) * trackGain;
                l *= g;
                r *= g;
                const float fi = fadeGain(clip.fadeIn, inClip, true);
                l *= fi;
                r *= fi;
                if (clip.fadeOut.len > 0 && inClip >= clip.length - clip.fadeOut.len) {
                    const long long tail = inClip - (clip.length - clip.fadeOut.len);
                    const float fo = 1.0f - fadeGain(clip.fadeOut, tail, true);
                    l *= fo;
                    r *= fo;
                }
                outStereo[i * 2] += l * lPan;
                outStereo[i * 2 + 1] += r * rPan;
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
    rebuildPlan(doc);
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
        render(pos, block.data(), want, rate);
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

bool writeMonoPcmWav(const std::string& path, const float* pcm, int frames, int sampleRate, std::string& err) {
    if (!pcm || frames <= 0 || sampleRate <= 0) {
        err = "Nothing to write.";
        return false;
    }
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "wb");
#else
    f = fopen(path.c_str(), "wb");
#endif
    if (!f) {
        err = "Could not write WAV.";
        return false;
    }
    const int channels = 1;
    const int bits = 16;
    const int byteRate = sampleRate * channels * bits / 8;
    const int dataBytes = frames * channels * (bits / 8);
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    w32((uint32_t)(36 + dataBytes));
    fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(1);
    w16((uint16_t)channels);
    w32((uint32_t)sampleRate);
    w32((uint32_t)byteRate);
    w16((uint16_t)(channels * bits / 8));
    w16((uint16_t)bits);
    fwrite("data", 1, 4, f);
    w32((uint32_t)dataBytes);
    for (int i = 0; i < frames; ++i) {
        float s = std::clamp(pcm[i], -1.0f, 1.0f);
        const int16_t v = (int16_t)(s * 32767.0f);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
    return true;
}

bool writeTestToneWav(const std::string& path, double hz, double seconds, int sampleRate, std::string& err) {
    Buffer b;
    b.sampleRate = sampleRate;
    fillSine(b, seconds, hz);
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "wb");
#else
    f = fopen(path.c_str(), "wb");
#endif
    if (!f) { err = "Could not write tone WAV."; return false; }
    const int channels = 2;
    const int bits = 16;
    const int byteRate = sampleRate * channels * bits / 8;
    const int dataBytes = b.frames * channels * (bits / 8);
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    w32((uint32_t)(36 + dataBytes));
    fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(1);
    w16((uint16_t)channels);
    w32((uint32_t)sampleRate);
    w32((uint32_t)byteRate);
    w16((uint16_t)(channels * bits / 8));
    w16((uint16_t)bits);
    fwrite("data", 1, 4, f);
    w32((uint32_t)dataBytes);
    for (int i = 0; i < b.frames; ++i) {
        float s = b.stereo[(size_t)i * 2];
        s = std::clamp(s, -1.0f, 1.0f);
        const int16_t v = (int16_t)(s * 32767.0f);
        fwrite(&v, 2, 1, f);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
    return true;
}

bool verifyToneInWav(const std::string& path, double expectHz, double sampleRate, std::string& err) {
    ma_decoder decoder;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS) { err = "Could not read render."; return false; }
    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total);
    const int rate = (int)decoder.outputSampleRate;
    if ((int)total < rate) { err = "Render too short."; ma_decoder_uninit(&decoder); return false; }
    float peak = 0.0f;
    int crossings = 0;
    float prev = 0.0f;
    bool havePrev = false;
    std::vector<float> chunk(4096);
    ma_uint64 read = 0;
    while (read < total) {
        ma_uint64 got = 0;
        ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &got);
        if (got == 0) break;
        for (ma_uint64 i = 0; i < got; ++i) {
            const float s = chunk[i];
            peak = std::max(peak, std::fabs(s));
            if (havePrev && ((prev <= 0 && s > 0) || (prev >= 0 && s < 0))) ++crossings;
            prev = s;
            havePrev = true;
        }
        read += got;
    }
    ma_decoder_uninit(&decoder);
    const double seconds = (double)total / (double)rate;
    const double zcPerSec = crossings / (2.0 * seconds);
    if (peak < 0.05f) { err = "Peak too low."; return false; }
    if (std::fabs(zcPerSec - expectHz) > expectHz * 0.12) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Tone frequency mismatch (got %.1f Hz, peak %.3f).", zcPerSec, peak);
        err = buf;
        return false;
    }
    return true;
}

Peaks peaksForClip(const MixPlan::ClipPlay& clip, int bucketCount) {
    if (clip.audio) return buildPeaks(*clip.audio, bucketCount);
    return {};
}

const Peaks& peaksForClipPixels(const std::string& sourceId, const Buffer& audio, const Session::Clip& clip, int pixelColumns) {
    const int cols = std::max(1, pixelColumns);
    const std::string key = sourceId + ":" + clip.id + ":" + std::to_string(cols) + ":" + std::to_string(clip.offset) + ":"
                            + std::to_string(clip.length);
    std::lock_guard<std::mutex> g(peakMtx);
    auto& p = peakCache[key];
    if (p.buckets != 0) return p;
    if (audio.frames <= 0) return p;
    const int off = (int)std::clamp(clip.offset, 0LL, (long long)audio.frames - 1);
    const int len = (int)std::clamp(clip.length, 0LL, (long long)audio.frames - off);
    const int end = off + len;
    p = buildPeaksSpan(audio, off, end, cols);
    return p;
}

} // namespace Mix