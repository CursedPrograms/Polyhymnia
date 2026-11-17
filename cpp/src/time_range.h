// Half-open sample range [start, end) (after SoundCraft crates/time/src/lib.rs Range).
#pragma once

#include <algorithm>
#include <limits>

namespace Time {

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
    long long len() const {
        const long long d = end - start;
        return d > 0 ? d : 0;
    }

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

    // after SoundCraft (crates/time/src/lib.rs Range::shifted)
    Range shifted(long long by) const;
};

inline Range Range::shifted(long long by) const { return {satAdd(start, by), satAdd(end, by)}; }

} // namespace Time