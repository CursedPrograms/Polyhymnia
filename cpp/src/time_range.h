// Half-open sample range [start, end) (after SoundCraft crates/time/src/lib.rs Range).
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace Time {

// after SoundCraft i64::saturating_add
inline long long satAdd(long long a, long long b) {
    if (b > 0 && a > std::numeric_limits<long long>::max() - b) return std::numeric_limits<long long>::max();
    if (b < 0 && a < std::numeric_limits<long long>::min() - b) return std::numeric_limits<long long>::min();
    return a + b;
}

// after SoundCraft i64::saturating_sub
inline long long satSub(long long a, long long b) {
    if (b > 0 && a < std::numeric_limits<long long>::min() + b) return std::numeric_limits<long long>::min();
    if (b < 0 && a > std::numeric_limits<long long>::max() + b) return std::numeric_limits<long long>::max();
    return a - b;
}

// after SoundCraft i64::saturating_mul
inline long long satMul(long long a, long long b) {
    if (a == 0 || b == 0) return 0;
    if (a > 0 && b > 0 && a > std::numeric_limits<long long>::max() / b) return std::numeric_limits<long long>::max();
    if (a > 0 && b < 0 && b < std::numeric_limits<long long>::min() / a) return std::numeric_limits<long long>::min();
    if (a < 0 && b > 0 && a < std::numeric_limits<long long>::min() / b) return std::numeric_limits<long long>::min();
    if (a < 0 && b < 0 && a < std::numeric_limits<long long>::max() / b) return std::numeric_limits<long long>::max();
    return a * b;
}

// after SoundCraft i64::saturating_neg
inline int64_t satNeg(int64_t a) {
    return a == std::numeric_limits<int64_t>::min() ? std::numeric_limits<int64_t>::max() : -a;
}

// after SoundCraft i64::unsigned_abs
inline uint64_t unsignedAbsI64(int64_t v) {
    return v < 0 ? 0ULL - static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
}

// after SoundCraft i64::div_euclid (for b > 0)
inline int64_t divEuclidI64(int64_t a, int64_t b) {
    if (b <= 0) return 0;
    int64_t q = a / b;
    const int64_t r = a % b;
    if (r < 0) --q;
    return q;
}

// after SoundCraft i64::rem_euclid (for b > 0)
inline int64_t remEuclidI64(int64_t a, int64_t b) {
    if (b <= 0) return 0;
    int64_t r = a % b;
    if (r < 0) r += b;
    return r;
}

struct Range {
    long long start = 0;
    long long end = 0;

    // after SoundCraft (crates/time/src/lib.rs Range::new)
    static Range newRange(long long a, long long b) {
        if (a <= b) return {a, b};
        return {b, a};
    }

    // after SoundCraft (crates/time/src/lib.rs Range::point)
    static Range point(long long at) { return {at, at}; }

    // after SoundCraft (crates/time/src/lib.rs Range::len)
    long long len() const { return satSub(end, start); }

    // after SoundCraft (crates/time/src/lib.rs Range::is_empty)
    bool isEmpty() const { return end <= start; }

    // after SoundCraft (crates/time/src/lib.rs Range::contains)
    bool contains(long long s) const { return s >= start && s < end; }

    // after SoundCraft (crates/time/src/lib.rs Range::overlaps)
    bool overlaps(const Range& o) const { return start < o.end && o.start < end; }

    // after SoundCraft (crates/time/src/lib.rs Range::intersect)
    bool intersect(const Range& o, Range* out) const {
        const long long s = std::max(start, o.start);
        const long long e = std::min(end, o.end);
        if (s >= e) return false;
        if (out) *out = {s, e};
        return true;
    }

    std::optional<Range> intersectOpt(const Range& o) const {
        Range r;
        if (intersect(o, &r)) return r;
        return std::nullopt;
    }

    // after SoundCraft (crates/time/src/lib.rs Range::shifted)
    Range shifted(long long by) const { return {satAdd(start, by), satAdd(end, by)}; }

    bool operator==(const Range& o) const { return start == o.start && end == o.end; }
    bool operator!=(const Range& o) const { return !(*this == o); }
};

} // namespace Time
