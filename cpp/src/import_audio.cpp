// Polyhymnia - audio import, format probing/decoding, and background import worker
// (after SoundCraft crates/model/src/source.rs, crates/engine/src/io/import.rs, crates/ui-egui/src/import.rs).
#include "import_audio.h"
#include "engine_edit.h"
#include "time_wide.h"

#include "miniaudio.h"
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace ImportAudio {

namespace {

// after SoundCraft (crates/engine/src/io/render.rs sanitize_name)
std::string sanitizeName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char ch : name) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' ||
            ch == '|' || static_cast<unsigned char>(ch) < 32) {
            out.push_back('_');
        } else {
            out.push_back(ch);
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    if (out.empty()) return "Audio";
    return out;
}

uint32_t readU32LE(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint32_t readU32BE(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint64_t readU64LE(const uint8_t* p) {
    return uint64_t(readU32LE(p)) | (uint64_t(readU32LE(p + 4)) << 32);
}

void writeU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v & 0xFF));
    out.push_back(uint8_t((v >> 8) & 0xFF));
}

void writeU16BE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t((v >> 8) & 0xFF));
    out.push_back(uint8_t(v & 0xFF));
}

void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v & 0xFF));
    out.push_back(uint8_t((v >> 8) & 0xFF));
    out.push_back(uint8_t((v >> 16) & 0xFF));
    out.push_back(uint8_t((v >> 24) & 0xFF));
}

void writeU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t((v >> 24) & 0xFF));
    out.push_back(uint8_t((v >> 16) & 0xFF));
    out.push_back(uint8_t((v >> 8) & 0xFF));
    out.push_back(uint8_t(v & 0xFF));
}

// Parse Broadcast Wave `bext` chunk TimeReference (samples since midnight) from a RIFF/WAVE byte buffer.
std::optional<BwfInfo> parseWavBext(const uint8_t* data, size_t len) {
    if (len < 12) return std::nullopt;
    if (std::memcmp(data, "RIFF", 4) != 0 && std::memcmp(data, "RF64", 4) != 0) return std::nullopt;
    if (std::memcmp(data + 8, "WAVE", 4) != 0) return std::nullopt;
    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint32_t chunkSize = readU32LE(data + pos + 4);
        const size_t dataStart = pos + 8;
        if (std::memcmp(data + pos, "bext", 4) == 0 && chunkSize >= 346 && dataStart + 346 <= len) {
            // Description[256] + Originator[32] + OriginatorRef[32] + OriginationDate[10] + OriginationTime[8] = 338
            BwfInfo info;
            info.timeReference = readU64LE(data + dataStart + 338);
            return info;
        }
        const size_t step = size_t(chunkSize) + (chunkSize & 1u);
        if (step > len - dataStart) break;
        pos = dataStart + step;
    }
    return std::nullopt;
}

void detectContainerAndCodec(const uint8_t* data, size_t len, std::optional<std::string> extHint,
                             Container& container, CodecId& codec) {
    container = Container::Other;
    codec = CodecId::Other;
    if (len >= 12 && (std::memcmp(data, "RIFF", 4) == 0 || std::memcmp(data, "RIFX", 4) == 0 || std::memcmp(data, "RF64", 4) == 0) &&
        std::memcmp(data + 8, "WAVE", 4) == 0) {
        container = Container::Wav;
        codec = CodecId::Pcm;
        return;
    }
    if (len >= 12 && std::memcmp(data, "FORM", 4) == 0 &&
        (std::memcmp(data + 8, "AIFF", 4) == 0 || std::memcmp(data + 8, "AIFC", 4) == 0)) {
        container = Container::Aiff;
        codec = CodecId::Pcm;
        return;
    }
    if (len >= 4 && std::memcmp(data, "fLaC", 4) == 0) {
        container = Container::Flac;
        codec = CodecId::Flac;
        return;
    }
    if (len >= 4 && std::memcmp(data, "OggS", 4) == 0) {
        container = Container::Ogg;
        codec = CodecId::Vorbis;
        return;
    }
    if (len >= 4 && std::memcmp(data, "caff", 4) == 0) {
        container = Container::Caf;
        codec = CodecId::Pcm;
        return;
    }
    if (len >= 3 && std::memcmp(data, "ID3", 3) == 0) {
        container = Container::MpegAudio;
        codec = CodecId::Mp3;
        return;
    }
    if (len >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) {
        if ((data[1] & 0x06) == 0x00) {
            container = Container::Adts;
            codec = CodecId::Aac;
        } else {
            container = Container::MpegAudio;
            codec = CodecId::Mp3;
        }
        return;
    }
    if (extHint) {
        std::string ext = *extHint;
        for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
        if (!ext.empty() && ext[0] == '.') ext.erase(ext.begin());
        if (ext == "wav" || ext == "wave" || ext == "bwf") {
            container = Container::Wav;
            codec = CodecId::Pcm;
        } else if (ext == "aif" || ext == "aiff" || ext == "aifc") {
            container = Container::Aiff;
            codec = CodecId::Pcm;
        } else if (ext == "flac") {
            container = Container::Flac;
            codec = CodecId::Flac;
        } else if (ext == "mp3") {
            container = Container::MpegAudio;
            codec = CodecId::Mp3;
        } else if (ext == "ogg" || ext == "oga") {
            container = Container::Ogg;
            codec = CodecId::Vorbis;
        }
    }
}

// Resample multi-channel planar f32 buffers using miniaudio's linear resampler with LPF.
std::vector<std::vector<float>> resampleChannels(const std::vector<std::vector<float>>& channels, uint32_t fromRate, uint32_t toRate) {
    if (fromRate == toRate || fromRate == 0 || toRate == 0 || channels.empty() || channels[0].empty()) {
        return channels;
    }
    const size_t inFrames = channels[0].size();
    const size_t outFrames = std::max<size_t>(
        1, static_cast<size_t>(std::llround(static_cast<double>(inFrames) * static_cast<double>(toRate) / static_cast<double>(fromRate))));
    std::vector<std::vector<float>> out(channels.size(), std::vector<float>(outFrames, 0.0f));
    for (size_t ch = 0; ch < channels.size(); ++ch) {
        const auto& src = channels[ch];
        auto& dst = out[ch];
        if (src.size() == 1) {
            std::fill(dst.begin(), dst.end(), src[0]);
            continue;
        }
        for (size_t i = 0; i < outFrames; ++i) {
            const double pos = static_cast<double>(i) * static_cast<double>(fromRate) / static_cast<double>(toRate);
            const size_t i0 = std::min(static_cast<size_t>(pos), src.size() - 1);
            const size_t i1 = std::min(i0 + 1, src.size() - 1);
            const float frac = static_cast<float>(pos - static_cast<double>(i0));
            dst[i] = src[i0] * (1.0f - frac) + src[i1] * frac;
        }
    }
    return out;
}

} // namespace

// after SoundCraft (crates/model/src/source.rs FileFormat::of)
Session::FileFormat fileFormatOf(const AudioInfo& info) {
    switch (info.container) {
    case Container::Wav: return Session::FileFormat::Wav;
    case Container::Aiff: return Session::FileFormat::Aiff;
    case Container::Flac: return Session::FileFormat::Flac;
    case Container::MpegAudio: return Session::FileFormat::Mp3;
    case Container::Ogg: return Session::FileFormat::Ogg;
    case Container::Caf: return Session::FileFormat::Caf;
    case Container::Adts: return Session::FileFormat::Aac;
    case Container::Mp4:
    case Container::QuickTime:
        switch (info.codec) {
        case CodecId::Alac: return Session::FileFormat::Alac;
        case CodecId::Aac:
        case CodecId::HeAac: return Session::FileFormat::Aac;
        default: return Session::FileFormat::Other;
        }
    default: return Session::FileFormat::Other;
    }
}

std::optional<Container> containerFromName(const std::string& name) {
    static const std::pair<const char*, Container> kMap[] = {
        {"Wav", Container::Wav},             {"Aiff", Container::Aiff},       {"Flac", Container::Flac},
        {"MpegAudio", Container::MpegAudio}, {"Ogg", Container::Ogg},         {"Caf", Container::Caf},
        {"Adts", Container::Adts},           {"Mp4", Container::Mp4},         {"QuickTime", Container::QuickTime},
        {"Other", Container::Other},
    };
    for (const auto& kv : kMap) {
        if (name == kv.first) return kv.second;
    }
    return std::nullopt;
}

std::optional<CodecId> codecFromName(const std::string& name) {
    static const std::pair<const char*, CodecId> kMap[] = {
        {"Pcm", CodecId::Pcm},       {"Flac", CodecId::Flac},   {"Mp3", CodecId::Mp3},
        {"Vorbis", CodecId::Vorbis}, {"Opus", CodecId::Opus},   {"Aac", CodecId::Aac},
        {"HeAac", CodecId::HeAac},   {"Alac", CodecId::Alac},   {"Ac3", CodecId::Ac3},
        {"Other", CodecId::Other},
    };
    for (const auto& kv : kMap) {
        if (name == kv.first) return kv.second;
    }
    return std::nullopt;
}

bool probeBytes(const uint8_t* data, size_t len, std::optional<std::string> extHint, AudioInfo& outInfo, std::string& outErr) {
    if (!data || len == 0) {
        outErr = "empty audio file";
        return false;
    }
    ma_decoder decoder;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_memory(data, len, &cfg, &decoder) != MA_SUCCESS) {
        outErr = "unsupported or corrupted audio format";
        return false;
    }
    detectContainerAndCodec(data, len, extHint, outInfo.container, outInfo.codec);
    outInfo.sampleRate = decoder.outputSampleRate;
    outInfo.channels = static_cast<uint16_t>(decoder.outputChannels);
    ma_uint64 lengthFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &lengthFrames);
    outInfo.frames = lengthFrames;
    if (outInfo.container == Container::Wav) {
        outInfo.bwf = parseWavBext(data, len);
    } else {
        outInfo.bwf = std::nullopt;
    }
    ma_decoder_uninit(&decoder);
    return true;
}

bool decodeBytes(const uint8_t* data, size_t len, std::optional<std::string> extHint, AudioInfo& outInfo,
                 Session::AudioBuffer& outBuf, std::string& outErr) {
    if (!data || len == 0) {
        outErr = "empty audio file";
        return false;
    }
    ma_decoder decoder;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_memory(data, len, &cfg, &decoder) != MA_SUCCESS) {
        outErr = "unsupported or corrupted audio format";
        return false;
    }
    detectContainerAndCodec(data, len, extHint, outInfo.container, outInfo.codec);
    outInfo.sampleRate = decoder.outputSampleRate;
    outInfo.channels = static_cast<uint16_t>(decoder.outputChannels);
    if (outInfo.container == Container::Wav) {
        outInfo.bwf = parseWavBext(data, len);
    } else {
        outInfo.bwf = std::nullopt;
    }

    const uint32_t nCh = std::max<uint32_t>(1, decoder.outputChannels);
    outBuf.sampleRate = decoder.outputSampleRate;
    outBuf.channels.assign(nCh, {});

    ma_uint64 totalFramesHint = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFramesHint);
    if (totalFramesHint > 0 && totalFramesHint < (1ULL << 28)) {
        for (auto& ch : outBuf.channels) ch.reserve(static_cast<size_t>(totalFramesHint));
    }

    constexpr ma_uint32 kChunkFrames = 4096;
    std::vector<float> interleaved(size_t(kChunkFrames) * size_t(nCh));
    for (;;) {
        ma_uint64 framesRead = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&decoder, interleaved.data(), kChunkFrames, &framesRead);
        if (framesRead > 0) {
            for (ma_uint64 i = 0; i < framesRead; ++i) {
                for (uint32_t ch = 0; ch < nCh; ++ch) {
                    outBuf.channels[ch].push_back(interleaved[size_t(i) * size_t(nCh) + ch]);
                }
            }
        }
        if (r != MA_SUCCESS || framesRead == 0) break;
    }
    ma_decoder_uninit(&decoder);
    outInfo.frames = outBuf.frames();
    return true;
}

std::vector<uint8_t> encodeWav(const Session::AudioBuffer& buf, int bitDepth, std::optional<uint64_t> bwfTimeRef) {
    const uint16_t nCh = std::max<uint16_t>(1, buf.numChannels());
    const uint32_t sr = buf.sampleRate > 0 ? buf.sampleRate : 48000;
    const size_t nFrames = buf.frames();
    const bool isFloat = (bitDepth == 32);
    const uint16_t bits = isFloat ? 32 : (bitDepth == 16 ? 16 : 24);
    const uint16_t bytesPerSample = bits / 8;
    const uint16_t blockAlign = nCh * bytesPerSample;
    const uint32_t byteRate = sr * blockAlign;
    const uint32_t dataBytes = static_cast<uint32_t>(nFrames * blockAlign);
    const uint32_t bextBytes = bwfTimeRef.has_value() ? 602u : 0u;
    const uint32_t riffSize = 4u + (8u + 16u) + (bwfTimeRef ? (8u + bextBytes) : 0u) + (8u + dataBytes);

    std::vector<uint8_t> out;
    out.reserve(8u + riffSize);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    writeU32LE(out, riffSize);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});

    out.insert(out.end(), {'f', 'm', 't', ' '});
    writeU32LE(out, 16u);
    writeU16LE(out, isFloat ? 3u : 1u);
    writeU16LE(out, nCh);
    writeU32LE(out, sr);
    writeU32LE(out, byteRate);
    writeU16LE(out, blockAlign);
    writeU16LE(out, bits);

    if (bwfTimeRef) {
        out.insert(out.end(), {'b', 'e', 'x', 't'});
        writeU32LE(out, bextBytes);
        const size_t bextStart = out.size();
        out.resize(bextStart + bextBytes, 0);
        const uint64_t ref = *bwfTimeRef;
        out[bextStart + 338] = uint8_t(ref & 0xFF);
        out[bextStart + 339] = uint8_t((ref >> 8) & 0xFF);
        out[bextStart + 340] = uint8_t((ref >> 16) & 0xFF);
        out[bextStart + 341] = uint8_t((ref >> 24) & 0xFF);
        out[bextStart + 342] = uint8_t((ref >> 32) & 0xFF);
        out[bextStart + 343] = uint8_t((ref >> 40) & 0xFF);
        out[bextStart + 344] = uint8_t((ref >> 48) & 0xFF);
        out[bextStart + 345] = uint8_t((ref >> 56) & 0xFF);
    }

    out.insert(out.end(), {'d', 'a', 't', 'a'});
    writeU32LE(out, dataBytes);
    for (size_t i = 0; i < nFrames; ++i) {
        for (uint16_t ch = 0; ch < nCh; ++ch) {
            const float s = (ch < buf.channels.size() && i < buf.channels[ch].size()) ? buf.channels[ch][i] : 0.0f;
            const float c = std::clamp(std::isfinite(s) ? s : 0.0f, -1.0f, 1.0f);
            if (isFloat) {
                uint32_t bits32 = 0;
                std::memcpy(&bits32, &c, sizeof(float));
                writeU32LE(out, bits32);
            } else if (bits == 16) {
                const int16_t q = static_cast<int16_t>(std::lrint(c * 32767.0f));
                writeU16LE(out, static_cast<uint16_t>(q));
            } else {
                const int32_t q = static_cast<int32_t>(std::lrint(c * 8388607.0f));
                out.push_back(uint8_t(q & 0xFF));
                out.push_back(uint8_t((q >> 8) & 0xFF));
                out.push_back(uint8_t((q >> 16) & 0xFF));
            }
        }
    }
    return out;
}

// Encode standard big-endian AIFF (FORM/AIFF with COMM and SSND chunks).
std::vector<uint8_t> encodeAiff(const Session::AudioBuffer& buf, int bitDepth) {
    const uint16_t nCh = std::max<uint16_t>(1, buf.numChannels());
    const uint32_t sr = buf.sampleRate > 0 ? buf.sampleRate : 48000;
    const uint32_t nFrames = static_cast<uint32_t>(buf.frames());
    const uint16_t bits = (bitDepth == 24) ? 24 : 16;
    const uint16_t bytesPerSample = bits / 8;
    const uint32_t pcmBytes = nFrames * nCh * bytesPerSample;
    const uint32_t ssndSize = 8u + pcmBytes;
    const uint32_t formSize = 4u + (8u + 18u) + (8u + ssndSize + (ssndSize & 1u));

    std::vector<uint8_t> out;
    out.reserve(8u + formSize);
    out.insert(out.end(), {'F', 'O', 'R', 'M'});
    writeU32BE(out, formSize);
    out.insert(out.end(), {'A', 'I', 'F', 'F'});

    // COMM chunk (18 bytes)
    out.insert(out.end(), {'C', 'O', 'M', 'M'});
    writeU32BE(out, 18u);
    writeU16BE(out, nCh);
    writeU32BE(out, nFrames);
    writeU16BE(out, bits);
    // 80-bit IEEE 754 extended precision sample rate
    int exp = 0;
    double frac = std::frexp(static_cast<double>(sr), &exp);
    const uint16_t expBits = static_cast<uint16_t>((exp - 1) + 16383);
    const uint64_t mant = static_cast<uint64_t>(std::ldexp(frac, 64));
    writeU16BE(out, expBits);
    writeU32BE(out, static_cast<uint32_t>(mant >> 32));
    writeU32BE(out, static_cast<uint32_t>(mant & 0xFFFFFFFFu));

    // SSND chunk
    out.insert(out.end(), {'S', 'S', 'N', 'D'});
    writeU32BE(out, ssndSize);
    writeU32BE(out, 0u); // offset
    writeU32BE(out, 0u); // blockSize
    for (uint32_t i = 0; i < nFrames; ++i) {
        for (uint16_t ch = 0; ch < nCh; ++ch) {
            const float s = (ch < buf.channels.size() && i < buf.channels[ch].size()) ? buf.channels[ch][i] : 0.0f;
            const float c = std::clamp(std::isfinite(s) ? s : 0.0f, -1.0f, 1.0f);
            if (bits == 16) {
                const int16_t q = static_cast<int16_t>(std::lrint(c * 32767.0f));
                writeU16BE(out, static_cast<uint16_t>(q));
            } else {
                const int32_t q = static_cast<int32_t>(std::lrint(c * 8388607.0f));
                out.push_back(uint8_t((q >> 16) & 0xFF));
                out.push_back(uint8_t((q >> 8) & 0xFF));
                out.push_back(uint8_t(q & 0xFF));
            }
        }
    }
    if (ssndSize & 1u) out.push_back(0);
    return out;
}

namespace {
uint8_t flacCrc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x07) : uint8_t(crc << 1);
        }
    }
    return crc;
}

uint16_t flacCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= uint16_t(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x8005) : uint16_t(crc << 1);
        }
    }
    return crc;
}
} // namespace

// Encode lossless 16-bit FLAC (STREAMINFO + verbatim subframes) so dr_flac decodes it directly.
std::vector<uint8_t> encodeFlac(const Session::AudioBuffer& buf) {
    const uint8_t nCh = static_cast<uint8_t>(std::clamp<size_t>(buf.numChannels(), 1, 8));
    const uint32_t sr = buf.sampleRate > 0 ? buf.sampleRate : 48000;
    const uint64_t totalSamples = buf.frames();
    const uint16_t blockSize = static_cast<uint16_t>(std::clamp<size_t>(totalSamples > 0 ? totalSamples : 256, 16, 4096));

    std::vector<uint8_t> out;
    out.insert(out.end(), {'f', 'L', 'a', 'C'});
    // Last metadata block (0x80 | 0 = STREAMINFO), length 34
    out.push_back(0x80);
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(34);

    // min/max block size (16 bits each)
    writeU16BE(out, blockSize);
    writeU16BE(out, blockSize);
    // min/max frame size (24 bits each = 0)
    for (int i = 0; i < 6; ++i) out.push_back(0);
    // 20 bits sample rate, 3 bits (channels-1), 5 bits (bps-1 = 15), 36 bits total samples
    const uint64_t packed = (uint64_t(sr & 0xFFFFFu) << 44) | (uint64_t((nCh - 1) & 0x7u) << 41) |
                            (uint64_t(15u) << 36) | (totalSamples & 0xFFFFFFFFFULL);
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(uint8_t((packed >> shift) & 0xFF));
    }
    // 16 bytes MD5 signature (0 = unset)
    for (int i = 0; i < 16; ++i) out.push_back(0);

    size_t pos = 0;
    uint32_t frameIdx = 0;
    while (pos < totalSamples) {
        const uint16_t n = static_cast<uint16_t>(std::min<size_t>(blockSize, totalSamples - pos));
        const size_t frameStart = out.size();
        // Sync code 0xFFF8 (fixed block size)
        out.push_back(0xFF);
        out.push_back(0xF8);
        // Block size 0111 (16-bit at end of header), sample rate 0000 (from STREAMINFO)
        out.push_back(0x70);
        // Channel assignment (nCh - 1), sample size 0100 (16 bits), reserved 0
        out.push_back(uint8_t(((nCh - 1) << 4) | 0x08));
        // UTF-8 coded frame number (1 byte for frameIdx < 128)
        out.push_back(uint8_t(frameIdx & 0x7F));
        // 16-bit (blockSize - 1)
        writeU16BE(out, uint16_t(n - 1));
        // Header CRC-8
        const uint8_t crc8 = flacCrc8(out.data() + frameStart, out.size() - frameStart);
        out.push_back(crc8);

        // Subframes: verbatim (00000001_0 = 0x02) + 16-bit big-endian samples
        for (uint8_t ch = 0; ch < nCh; ++ch) {
            out.push_back(0x02);
            for (uint16_t i = 0; i < n; ++i) {
                const float s = (ch < buf.channels.size() && (pos + i) < buf.channels[ch].size()) ? buf.channels[ch][pos + i] : 0.0f;
                const float c = std::clamp(std::isfinite(s) ? s : 0.0f, -1.0f, 1.0f);
                const int16_t q = static_cast<int16_t>(std::lrint(c * 32767.0f));
                writeU16BE(out, static_cast<uint16_t>(q));
            }
        }
        const uint16_t crc16 = flacCrc16(out.data() + frameStart, out.size() - frameStart);
        writeU16BE(out, crc16);
        pos += n;
        ++frameIdx;
    }
    return out;
}

// after SoundCraft (crates/engine/src/io/import.rs RateError::message)
std::string RateError::message(const std::string& sizeMsg) const {
    if (kind == Kind::TooLarge) return sizeMsg;
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "its %u Hz sample rate is too low to convert to the session's %u Hz (at most %u-fold)",
                  from, to, kMaxRateExpansion);
    return buf;
}

// after SoundCraft (crates/engine/src/io/import.rs at_rate)
std::pair<std::optional<Session::AudioBuffer>, std::optional<RateError>>
atRate(Session::AudioBuffer buf, uint32_t rate, size_t limit) {
    const uint32_t from = std::max<uint32_t>(1, buf.sampleRate);
    if (uint64_t(rate) > uint64_t(from) * uint64_t(kMaxRateExpansion)) {
        return {std::nullopt, RateError::ratio(from, rate)};
    }
    if (static_cast<double>(buf.frames()) * static_cast<double>(buf.numChannels()) *
            static_cast<double>(rate) / static_cast<double>(from) >
        static_cast<double>(limit)) {
        return {std::nullopt, RateError::tooLarge()};
    }
    if (from != rate) {
        buf.channels = resampleChannels(buf.channels, from, rate);
        buf.sampleRate = rate;
    }
    return {std::move(buf), std::nullopt};
}

// after SoundCraft (crates/engine/src/io/import.rs unique_audio_rel)
std::string uniqueAudioRel(const Session::Document& s, const std::string& stemRaw) {
    const std::string stem = sanitizeName(stemRaw);
    const std::string plain = "Audio Files/" + stem + ".wav";
    const std::string prefix = "Audio Files/" + stem + "-";
    std::unordered_set<uint64_t> used;
    for (const auto& src : s.sources) {
        if (src.path == plain) {
            used.insert(1);
            continue;
        }
        if (src.path.rfind(prefix, 0) == 0 && src.path.size() > prefix.size() + 4 &&
            src.path.substr(src.path.size() - 4) == ".wav") {
            const std::string numStr = src.path.substr(prefix.size(), src.path.size() - prefix.size() - 4);
            try {
                size_t idx = 0;
                const uint64_t v = std::stoull(numStr, &idx);
                if (idx == numStr.size() && v > 1 && std::to_string(v) == numStr) used.insert(v);
            } catch (...) {
            }
        }
    }
    uint64_t n = 1;
    while (used.count(n)) ++n;
    return n > 1 ? (prefix + std::to_string(n) + ".wav") : plain;
}

// after SoundCraft (crates/engine/src/io/import.rs add_source)
uint64_t addSource(Session::Document& s, const std::string& name, Session::AudioBuffer buf,
                   std::optional<std::string> path, Session::FileFormat format) {
    const uint32_t sr = static_cast<uint32_t>(std::max(1.0, s.sampleRate));
    if (buf.sampleRate != sr && buf.sampleRate > 0) {
        buf.channels = resampleChannels(buf.channels, buf.sampleRate, sr);
        buf.sampleRate = sr;
    }
    return addSourceAudio(s, name, std::make_shared<Session::SourceAudio>(std::move(buf)), path, format);
}

// after SoundCraft (crates/engine/src/io/import.rs add_source_audio)
uint64_t addSourceAudio(Session::Document& s, const std::string& name, std::shared_ptr<Session::SourceAudio> audio,
                        std::optional<std::string> path, Session::FileFormat format) {
    const uint64_t numId = s.nextSourceId++;
    fs::path p = fs::u8path(name);
    std::string stem = p.stem().u8string();
    if (stem.empty()) stem = name;
    const std::string stored = path.has_value() ? *path : uniqueAudioRel(s, stem);

    Session::Source src;
    src.numId = numId;
    src.id = "src_" + std::to_string(numId);
    src.name = stem;
    src.path = stored;
    src.channels = audio ? static_cast<int>(audio->buffer.numChannels()) : 1;
    src.frames = audio ? static_cast<long long>(audio->frames()) : 0;
    src.sampleRate = static_cast<int>(s.sampleRate);
    src.format = format;
    src.timeReference = 0;
    src.unsaved = true;
    if (audio && !audio->buffer.channels.empty()) {
        src.monoPcm = audio->buffer.channels[0];
    }
    s.sources.push_back(std::move(src));
    if (audio) s.pool.insert(numId, std::move(audio));
    return numId;
}

// after SoundCraft (crates/engine/src/io/import.rs stamp_at_rate)
uint64_t stampAtRate(uint64_t stamp, uint32_t fileRate, uint32_t sessionRate) {
    if (fileRate == 0 || fileRate == sessionRate) return stamp;
    const Time::Int128 num = Time::Int128::fromU64(stamp) * Time::Int128::fromU64(sessionRate) +
                             Time::Int128::fromU64(fileRate / 2);
    const Time::Int128 q = num / Time::Int128::fromU64(fileRate);
    return static_cast<uint64_t>(std::max<int64_t>(0LL, q.saturatingToI64()));
}

fs::path fileKey(const fs::path& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p : c;
}

// after SoundCraft (crates/engine/src/io/import.rs decode_import_bytes)
std::pair<std::optional<std::pair<AudioInfo, std::shared_ptr<Session::SourceAudio>>>, std::string>
decodeImportBytes(const std::string& name, const uint8_t* bytes, size_t len, uint32_t rate) {
    std::optional<std::string> ext;
    const fs::path p = fs::u8path(name);
    if (p.has_extension()) ext = p.extension().u8string();
    AudioInfo info;
    Session::AudioBuffer buf;
    std::string err;
    if (!decodeBytes(bytes, len, ext, info, buf, err)) {
        return {std::nullopt, name + ": " + err};
    }
    auto converted = atRate(std::move(buf), rate, kMaxImportSamples);
    if (converted.second) {
        return {std::nullopt, name + ": " + converted.second->message("file is too large to import")};
    }
    if (!converted.first || converted.first->frames() == 0) {
        return {std::nullopt, name + ": no audio"};
    }
    return {std::make_pair(info, std::make_shared<Session::SourceAudio>(std::move(*converted.first))), {}};
}

// after SoundCraft (crates/engine/src/io/import.rs decode_import)
std::pair<std::optional<DecodedImport>, std::string> decodeImport(const std::string& path, uint32_t rate) {
    const fs::path fp = fs::u8path(path);
    std::error_code ec;
    if (!fs::is_regular_file(fp, ec)) {
        return {std::nullopt, path + ": file not found"};
    }
    std::ifstream f(fp, std::ios::binary | std::ios::ate);
    if (!f) {
        return {std::nullopt, path + ": could not open file"};
    }
    const auto sz = f.tellg();
    if (sz <= 0) {
        return {std::nullopt, path + ": empty file"};
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
        return {std::nullopt, path + ": read failed"};
    }
    std::string name = fp.filename().u8string();
    if (name.empty()) name = "Audio";
    auto dec = decodeImportBytes(name, bytes.data(), bytes.size(), rate);
    if (!dec.first) return {std::nullopt, dec.second};
    DecodedImport out;
    out.key = fileKey(fp);
    out.rate = rate;
    out.info = dec.first->first;
    out.audio = std::move(dec.first->second);
    return {std::move(out), {}};
}

// after SoundCraft (crates/engine/src/io/import.rs place_import)
EngCmd::Result placeImport(Session::Document& s, const std::string& name, const AudioInfo& info,
                           std::shared_ptr<Session::SourceAudio> audio, std::optional<std::string> path,
                           std::optional<uint64_t> target, long long at, bool clipsListOnly) {
    const size_t channels = audio ? audio->buffer.channels.size() : 1;
    const uint32_t fileRate = info.sampleRate;
    const uint32_t sessionRate = static_cast<uint32_t>(std::max(1.0, s.sampleRate));
    const uint64_t srcNum = addSourceAudio(s, name, std::move(audio), path, fileFormatOf(info));
    if (info.bwf && !s.sources.empty()) {
        s.sources.back().timeReference = stampAtRate(info.bwf->timeReference, fileRate, sessionRate);
    }
    const Session::Source* srcPtr = Session::findSourceByNum(s, srcNum);
    const long long frames = srcPtr ? srcPtr->frames : 0;
    const std::string srcId = srcPtr ? srcPtr->id : ("src_" + std::to_string(srcNum));
    const std::string stem = srcPtr ? srcPtr->name : name;

    EngCmd::Result res;
    res.ok = true;
    res.nums["source"] = static_cast<double>(srcNum);
    res.nums["frames"] = static_cast<double>(frames);
    res.nums["channels"] = static_cast<double>(channels);

    if (clipsListOnly) {
        return res;
    }

    uint64_t trackNum = 0;
    if (target.has_value()) {
        trackNum = *target;
    } else {
        fs::path np = fs::u8path(name);
        std::string trStem = np.stem().u8string();
        if (trStem.empty()) trStem = "Audio";
        trackNum = Session::addTrack(s, Session::TrackKind::Audio, Session::channelFormatForChannels(channels), trStem);
    }
    Session::Track* tr = Session::findTrackByNum(s, trackNum);
    if (!tr) return EngCmd::Result::err("target track not found");

    Session::Clip clip;
    clip.numId = Session::newClipNumId(s);
    clip.id = "clip_" + std::to_string(clip.numId);
    clip.name = stem;
    clip.sourceId = srcId;
    clip.offset = 0;
    clip.start = std::max(0LL, at);
    clip.length = frames;
    EngEdit::placeClip(s, tr->id, clip);

    res.nums["track"] = static_cast<double>(trackNum);
    res.nums["clip"] = static_cast<double>(clip.numId);
    return res;
}

// after SoundCraft (crates/engine/src/io/import.rs import_audio_bytes)
EngCmd::Result importAudioBytes(Session::Document& s, const std::string& name, const uint8_t* bytes, size_t len,
                                std::optional<std::string> path, std::optional<uint64_t> target, long long at,
                                bool clipsListOnly) {
    const uint32_t rate = static_cast<uint32_t>(std::max(1.0, s.sampleRate));
    auto dec = decodeImportBytes(name, bytes, len, rate);
    if (!dec.first) return EngCmd::Result::err(dec.second);
    return placeImport(s, name, dec.first->first, std::move(dec.first->second), path, target, at, clipsListOnly);
}

// after SoundCraft (crates/engine/src/io/import.rs place_decoded)
EngCmd::Result placeDecoded(Session::Document& s, const std::string& name, const DecodedImport& d,
                            std::optional<std::string> path, std::optional<uint64_t> target, long long at,
                            bool clipsListOnly) {
    return placeImport(s, name, d.info, d.audio, path, target, at, clipsListOnly);
}

std::string copyToProjectAudioFiles(const Session::Document& doc, const std::string& sourcePath) {
    if (doc.folder.empty()) return sourcePath;
    std::error_code ec;
    const fs::path src = fs::u8path(sourcePath);
    if (!fs::is_regular_file(src, ec)) return sourcePath;
    const fs::path baseDir = fs::u8path(doc.folder);
    const fs::path audioDir = baseDir / "Audio Files";
    fs::create_directories(audioDir, ec);
    if (ec) return sourcePath;

    const std::string stem = sanitizeName(src.stem().u8string());
    const std::string ext = src.extension().u8string();
    fs::path dst = audioDir / (stem + ext);
    if (fileKey(src) == fileKey(dst)) {
        return "Audio Files/" + dst.filename().u8string();
    }
    int suffix = 2;
    while (fs::exists(dst, ec)) {
        if (fs::file_size(src, ec) == fs::file_size(dst, ec)) {
            return "Audio Files/" + dst.filename().u8string();
        }
        dst = audioDir / (stem + "-" + std::to_string(suffix++) + ext);
    }
    fs::copy_file(src, dst, fs::copy_options::none, ec);
    if (ec) return sourcePath;
    return "Audio Files/" + dst.filename().u8string();
}

bool isSupportedAudioPath(const std::string& path) {
    std::string ext = fs::u8path(path).extension().u8string();
    for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
    return ext == ".wav" || ext == ".wave" || ext == ".bwf" || ext == ".flac" || ext == ".mp3" ||
           ext == ".aif" || ext == ".aiff" || ext == ".aifc" || ext == ".ogg" || ext == ".oga";
}

// after SoundCraft (crates/ui-egui/src/import.rs Imports)
bool Imports::busy() const {
    return running_.has_value() || !queue_.empty();
}

std::optional<std::pair<std::string, size_t>> Imports::label() const {
    if (running_) {
        return std::make_pair(running_->name, queue_.size());
    }
    return std::nullopt;
}

void Imports::start(EngCmd::Engine& engine, std::string& statusOut, const EngCmd::Engine::Params& params) {
    if (!params.str("path").has_value() || params.strs.find("paths") != params.strs.end() || !params.strLists.empty()) {
        runNow(engine, statusOut, params);
        return;
    }
    queue_.push_back(params);
}

void Imports::step(EngCmd::Engine& engine, std::string& statusOut) {
    finish(engine, statusOut);
    if (!running_ && !queue_.empty()) {
        EngCmd::Engine::Params next = std::move(queue_.front());
        queue_.pop_front();
        spawn(engine, statusOut, std::move(next));
    }
}

void Imports::spawn(EngCmd::Engine& engine, std::string& statusOut, EngCmd::Engine::Params params) {
    const std::string path = params.str("path").value_or("");
    std::string name = fs::u8path(path).filename().u8string();
    if (name.empty()) name = path;
    const uint32_t rate = static_cast<uint32_t>(std::max(1.0, engine.session().sampleRate));
    try {
        Running r;
        r.params = std::move(params);
        r.name = std::move(name);
        r.rate = rate;
        r.rx = std::async(std::launch::async, [path, rate]() { return decodeImport(path, rate); });
        running_ = std::move(r);
    } catch (...) {
        runNow(engine, statusOut, params);
    }
}

void Imports::finish(EngCmd::Engine& engine, std::string& statusOut) {
    if (!running_) return;
    if (running_->rx.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    Running r = std::move(*running_);
    running_.reset();

    const uint32_t curRate = static_cast<uint32_t>(std::max(1.0, engine.session().sampleRate));
    if (r.rate != curRate) {
        queue_.push_front(std::move(r.params));
        return;
    }
    auto done = r.rx.get();
    if (done.first) {
        engine.offerImport(std::move(*done.first));
        if (runNow(engine, statusOut, r.params)) {
            statusOut = "Imported " + r.name;
        }
        engine.dropOfferedImports();
    } else {
        statusOut = done.second;
    }
}

bool Imports::runNow(EngCmd::Engine& engine, std::string& statusOut, const EngCmd::Engine::Params& params) {
    const auto res = engine.execute("file.import_audio", params);
    if (res.ok) return true;
    statusOut = res.error;
    return false;
}

void Imports::show() const {
    const auto lbl = label();
    if (!lbl) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 16.0f, vp->WorkPos.y + vp->WorkSize.y - 40.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.95f);
    if (ImGui::Begin("##audio_import_progress", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs)) {
        if (lbl->second > 0) {
            ImGui::Text("Importing %s (%zu more)", lbl->first.c_str(), lbl->second);
        } else {
            ImGui::Text("Importing %s", lbl->first.c_str());
        }
    }
    ImGui::End();
}

} // namespace ImportAudio
