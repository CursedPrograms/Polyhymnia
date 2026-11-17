// 128-bit integer math for sample/time conversions (after SoundCraft i128 use in crates/time).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <limits>

namespace Time {

class Int128 {
public:
    uint64_t lo = 0;
    int64_t hi = 0;

    Int128() = default;
    Int128(int64_t v) : lo(static_cast<uint64_t>(v)), hi(v < 0 ? -1 : 0) {}

    static Int128 fromParts(uint64_t l, int64_t h) {
        Int128 v;
        v.lo = l;
        v.hi = h;
        return v;
    }

    bool negative() const { return hi < 0; }

    Int128 operator-() const {
        const uint64_t nlo = ~lo + 1;
        int64_t nhi = ~hi;
        if (nlo == 0) ++nhi;
        return fromParts(nlo, nhi);
    }

    Int128 abs() const { return negative() ? -(*this) : *this; }

    static Int128 unsignedAbs(int64_t v) { return v == std::numeric_limits<int64_t>::min() ? fromParts(0, 0x8000000000000000ULL) : Int128(v < 0 ? -v : v); }

    bool operator==(const Int128& o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const Int128& o) const { return !(*this == o); }
    bool operator<(const Int128& o) const {
        if (hi != o.hi) return hi < o.hi;
        return lo < o.lo;
    }
    bool operator<=(const Int128& o) const { return *this < o || *this == o; }
    bool operator>(const Int128& o) const { return !(*this <= o); }
    bool operator>=(const Int128& o) const { return !(*this < o); }

    Int128& operator+=(const Int128& o);
    Int128 operator+(const Int128& o) const { Int128 r = *this; r += o; return r; }
    Int128& operator-=(const Int128& o) { return *this += (-o); }
    Int128 operator-(const Int128& o) const { return *this + (-o); }

    Int128 operator*(int64_t m) const;
    Int128 operator*(const Int128& o) const;

    static Int128 divRem(const Int128& num, const Int128& den, Int128* rem);
    static Int128 divEuclid(const Int128& num, const Int128& den);

    int64_t saturatingToI64() const;

    Int128 operator/(int64_t d) const;
    Int128 operator/(const Int128& d) const { return divRem(*this, d, nullptr); }

private:
    static void udiv128(uint64_t nlo, uint64_t nhi, uint64_t d, uint64_t* qlo, uint64_t* qhi, uint64_t* r);
};

inline Int128 mul64(int64_t a, int64_t b) { return Int128(a) * b; }

} // namespace Time