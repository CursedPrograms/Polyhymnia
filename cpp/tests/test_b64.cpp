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

} // namespace

void registerB64Tests() {
    Test::add("b64_rfc4648_vectors_round_trip", rfc4648VectorsRoundTrip);
    Test::add("b64_hostile_input_is_rejected", hostileBase64IsRejected);
}
