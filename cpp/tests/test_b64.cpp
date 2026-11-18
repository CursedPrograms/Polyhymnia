#include "b64.h"
#include "test_runner.h"

#include <string>
#include <vector>

namespace {

// after SoundCraft (crates/model/src/b64.rs rfc4648_vectors_round_trip)
bool rfc4648VectorsRoundTrip() {
    for (const auto& item : std::vector<std::pair<std::string, std::string>>{
             {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
             {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}}) {
        const std::vector<uint8_t> bytes(item.first.begin(), item.first.end());
        if (B64::encode(bytes) != item.second) return false;
        auto decoded = B64::decode(item.second);
        if (!decoded || *decoded != bytes) return false;
    }
    std::vector<uint8_t> all(256);
    for (size_t i = 0; i < all.size(); ++i) all[i] = static_cast<uint8_t>(i);
    if (B64::decode(B64::encode(all)) != all) return false;
    return B64::decode("Zm9v\nYmE=") == std::vector<uint8_t>({'f','o','o','b','a'}) &&
           B64::decode("Zm9vYmE") == std::vector<uint8_t>({'f','o','o','b','a'});
}

// after SoundCraft (crates/model/src/b64.rs hostile_input_is_rejected)
bool hostileBase64IsRejected() {
    for (const char* text : {"Z", "Zm9=v", "Zm9v!", "====", "Zg=", "\xc3\xa9", "Zg===", "Z==="}) {
        if (B64::decode(text)) return false;
    }
    return true;
}

// after SoundCraft (crates/model/src/b64.rs size_cap_counts_the_partial_quartet)
bool base64DecodedSizeIsCapped() {
    // At the maximum symbol count, this padded body still decodes to cap + 1 bytes.
    // The decoded-size check must reject it even though the encoded-size precheck allows it.
    const size_t maxSymbols = ((B64::kMaxDecodedBytes + 2) / 3) * 4;
    std::string padded(maxSymbols - 1, 'A');
    padded.push_back('=');
    if (B64::decode(padded).has_value()) return false;
    return !B64::decode(std::string(maxSymbols + 1, 'A')).has_value();
}

} // namespace

void registerB64Tests() {
    Test::add("b64_rfc4648_vectors_round_trip", rfc4648VectorsRoundTrip);
    Test::add("b64_hostile_input_is_rejected", hostileBase64IsRejected);
    Test::add("b64_size_cap_counts_the_partial_quartet", base64DecodedSizeIsCapped);
}
