// Polyhymnia - standard base64 (RFC 4648) with hostile-input validation (after SoundCraft crates/model/src/b64.rs).
#include "b64.h"

namespace B64 {

namespace {

constexpr char kAlphabet[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool isAsciiWhitespace(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\x0c';
}

// after SoundCraft (crates/model/src/b64.rs value)
std::optional<uint32_t> symValue(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return uint32_t(c - 'A');
    if (c >= 'a' && c <= 'z') return uint32_t(c - 'a' + 26);
    if (c >= '0' && c <= '9') return uint32_t(c - '0' + 52);
    if (c == '+') return 62u;
    if (c == '/') return 63u;
    return std::nullopt;
}

} // namespace

// after SoundCraft (crates/model/src/b64.rs encode)
std::string encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        const size_t chunkLen = (len - i < 3) ? (len - i) : 3;
        const uint8_t b0 = data[i];
        const uint8_t b1 = chunkLen > 1 ? data[i + 1] : 0;
        const uint8_t b2 = chunkLen > 2 ? data[i + 2] : 0;
        const uint32_t n = (uint32_t(b0) << 16) | (uint32_t(b1) << 8) | uint32_t(b2);
        out.push_back(kAlphabet[(n >> 18) & 63u]);
        out.push_back(kAlphabet[(n >> 12) & 63u]);
        out.push_back(chunkLen > 1 ? kAlphabet[(n >> 6) & 63u] : '=');
        out.push_back(chunkLen > 2 ? kAlphabet[n & 63u] : '=');
    }
    return out;
}

std::string encode(const std::vector<uint8_t>& data) {
    return encode(data.data(), data.size());
}

// after SoundCraft (crates/model/src/b64.rs decode)
std::optional<std::vector<uint8_t>> decode(const std::string& text) {
    const size_t maxSyms = ((kMaxDecodedBytes + 2) / 3) * 4;
    size_t nonWs = 0;
    for (unsigned char c : text) {
        if (!isAsciiWhitespace(c)) ++nonWs;
    }
    if (nonWs > maxSyms) return std::nullopt;

    std::vector<uint8_t> syms;
    syms.reserve(nonWs);
    for (unsigned char c : text) {
        if (!isAsciiWhitespace(c)) syms.push_back(c);
    }

    size_t bodyLen = syms.size();
    if (bodyLen >= 2 && syms[bodyLen - 1] == '=' && syms[bodyLen - 2] == '=') {
        bodyLen -= 2;
    } else if (bodyLen >= 1 && syms[bodyLen - 1] == '=') {
        bodyLen -= 1;
    }
    for (size_t i = 0; i < bodyLen; ++i) {
        if (syms[i] == '=') return std::nullopt;
    }
    if ((syms.size() != bodyLen && (syms.size() % 4) != 0) || (bodyLen % 4) == 1) {
        return std::nullopt;
    }
    const size_t decodedLen = (bodyLen / 4) * 3 + (bodyLen % 4) * 3 / 4;
    if (decodedLen > kMaxDecodedBytes) return std::nullopt;

    std::vector<uint8_t> out;
    out.reserve(decodedLen);
    for (size_t i = 0; i < bodyLen; i += 4) {
        const size_t chunkLen = (bodyLen - i < 4) ? (bodyLen - i) : 4;
        uint32_t n = 0;
        for (size_t j = 0; j < chunkLen; ++j) {
            auto v = symValue(syms[i + j]);
            if (!v) return std::nullopt;
            n |= (*v) << (18u - 6u * uint32_t(j));
        }
        const uint8_t bytes[3] = {uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
        for (size_t j = 0; j + 1 < chunkLen; ++j) out.push_back(bytes[j]);
    }
    return out;
}

} // namespace B64
