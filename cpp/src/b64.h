// Polyhymnia - standard base64 (RFC 4648) with hostile-input validation (after SoundCraft crates/model/src/b64.rs).
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace B64 {

// after SoundCraft (crates/model/src/b64.rs MAX_DECODED_BYTES)
constexpr size_t kMaxDecodedBytes = size_t(64) << 20;

// after SoundCraft (crates/model/src/b64.rs encode)
std::string encode(const uint8_t* data, size_t len);
std::string encode(const std::vector<uint8_t>& data);

// after SoundCraft (crates/model/src/b64.rs decode)
std::optional<std::vector<uint8_t>> decode(const std::string& text);

} // namespace B64
