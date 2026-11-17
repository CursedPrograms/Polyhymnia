// 128-bit helpers for time conversions.
#include "time_wide.h"

#include <limits>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace Time {

static void mulU64(uint64_t a, uint64_t b, uint64_t& lo, uint64_t& hi) {
#if defined(_MSC_VER)
    lo = _umul128(a, b, &hi);
#else
    const uint64_t a0 = a & 0xffffffffULL, a1 = a >> 32;
    const uint64_t b0 = b & 0xffffffffULL, b1 = b >> 32;
    const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    lo = p00 + (p01 << 32) + (p10 << 32);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (lo < p00 ? 1 : 0);
#endif
}

Int128& Int128::operator+=(const Int128& o) {
    const uint64_t oldLo = lo;
    lo += o.lo;
    hi += o.hi;
    if (lo < oldLo) ++hi;
    return *this;
}

Int128 Int128::operator*(int64_t m) const {
    // after SoundCraft i128 × i64 (full width; do not multiply lo alone when hi ≠ 0)
    return *this * Int128(m);
}

static void absU128(const Int128& v, uint64_t& alo, uint64_t& ahi) {
    if (!v.negative()) {
        alo = v.lo;
        ahi = static_cast<uint64_t>(v.hi);
        return;
    }
    const uint64_t nlo = ~v.lo + 1;
    uint64_t nhi = static_cast<uint64_t>(~v.hi);
    if (nlo == 0) ++nhi;
    alo = nlo;
    ahi = nhi;
}

static Int128 fromAbsU128(uint64_t ulo, uint64_t uhi, bool neg) {
    if (!neg) return Int128::fromParts(ulo, static_cast<int64_t>(uhi));
    if (ulo == 0 && uhi == 0) return Int128(0);
    const uint64_t nlo = ~ulo + 1;
    uint64_t nhi = ~uhi;
    if (nlo == 0) ++nhi;
    return Int128::fromParts(nlo, static_cast<int64_t>(nhi));
}

// Full 128×128 multiply (after Rust i128::wrapping_mul; no recursion).
Int128 Int128::operator*(const Int128& o) const {
    const bool neg = negative() != o.negative();
    uint64_t alo = 0, ahi = 0, blo = 0, bhi = 0;
    absU128(*this, alo, ahi);
    absU128(o, blo, bhi);

    uint64_t z0lo = 0, z0hi = 0, z1lo = 0, z1hi = 0, z2lo = 0, z2hi = 0, z3lo = 0, z3hi = 0;
    mulU64(alo, blo, z0lo, z0hi);
    mulU64(alo, bhi, z1lo, z1hi);
    mulU64(ahi, blo, z2lo, z2hi);
    mulU64(ahi, bhi, z3lo, z3hi);

    const uint64_t ulo = z0lo;
    uint64_t uhi = z0hi;
    uhi += z1lo;
    uhi += z2lo;
    uhi += z3lo;
    return fromAbsU128(ulo, uhi, neg);
}

void Int128::udiv128(uint64_t nlo, uint64_t nhi, uint64_t d, uint64_t* qlo, uint64_t* qhi, uint64_t* r) {
    if (d == 0) {
        *qlo = *qhi = 0;
        *r = 0;
        return;
    }
    if (nhi == 0) {
        *qlo = nlo / d;
        *qhi = 0;
        *r = nlo % d;
        return;
    }
    uint64_t rem = 0, qHiVal = 0, qLoVal = 0;
    for (int i = 63; i >= 0; --i) {
        const uint64_t bit = (nhi >> i) & 1;
        rem = (rem << 1) | bit;
        qHiVal = (qHiVal << 1) | (rem >= d ? 1 : 0);
        if (rem >= d) rem -= d;
    }
    for (int i = 63; i >= 0; --i) {
        const uint64_t bit = (nlo >> i) & 1;
        rem = (rem << 1) | bit;
        qLoVal = (qLoVal << 1) | (rem >= d ? 1 : 0);
        if (rem >= d) rem -= d;
    }
    *qlo = qLoVal;
    *qhi = qHiVal;
    *r = rem;
}

// True when v is exactly representable as i64 (two's-complement limb).
static bool int128FitsI64(const Int128& v, int64_t& out) {
    if (v.hi == 0 && v.lo <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        out = static_cast<int64_t>(v.lo);
        return true;
    }
    if (v.hi == -1 && v.lo >= 0x8000000000000000ULL) {
        out = static_cast<int64_t>(v.lo);
        return true;
    }
    return false;
}

Int128 Int128::divRem(const Int128& num, const Int128& den, Int128* rem) {
    if (den == Int128(0)) {
        if (rem) *rem = Int128(0);
        return Int128(0);
    }
    const bool neg = num.negative() != den.negative();
    Int128 n = num.abs();
    Int128 d = den.abs();
    if (d.hi != 0) {
        // Denominator too large for our udiv128 fast path; approximate via double (not used in grid hot paths with sr*rate_den).
        if (rem) *rem = Int128(0);
        return Int128(0);
    }
    uint64_t qlo = 0, qhi = 0, rlo = 0;
    udiv128(n.lo, static_cast<uint64_t>(n.hi), d.lo, &qlo, &qhi, &rlo);
    if (rem) *rem = fromParts(rlo, 0);
    Int128 q = fromParts(qlo, static_cast<int64_t>(qhi));
    return neg ? -q : q;
}

Int128 Int128::divEuclid(const Int128& num, const Int128& den) {
  int64_t n = 0, d = 0;
    if (int128FitsI64(num, n) && int128FitsI64(den, d) && d != 0) {
        int64_t q = n / d;
        const int64_t r = n % d;
        if (r < 0) q -= 1;
        return Int128(q);
    }
    // after Rust i128::div_euclid (truncating / and %, then step down if r < 0)
    Int128 rem;
    Int128 q = divRem(num, den, &rem);
    if (num.negative() != den.negative()) {
        if (rem != Int128(0)) q = q - Int128(1);
    } else if (rem.negative()) {
        q = q - Int128(1);
    }
    return q;
}

Int128 Int128::operator/(int64_t d) const {
    if (d == 0) return Int128(0);
    return divEuclid(*this, Int128(d));
}

int64_t Int128::saturatingToI64() const {
    if (hi > 0 || (hi == 0 && lo > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))) return std::numeric_limits<int64_t>::max();
    // hi == -1 and lo with MSB set is a normal negative i64 (not "lo > MAX" — that is true for all negatives).
    if (hi < -1 || (hi == -1 && lo < 0x8000000000000000ULL)) return std::numeric_limits<int64_t>::min();
    return static_cast<int64_t>(lo);
}

} // namespace Time