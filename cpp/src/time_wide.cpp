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
    hi = static_cast<int64_t>(static_cast<uint64_t>(hi) + static_cast<uint64_t>(o.hi) + (lo < oldLo ? 1ULL : 0ULL));
    return *this;
}

Int128 Int128::operator*(int64_t m) const {
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

// Full 128x128 multiply (after Rust i128::wrapping_mul).
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

// Full 128-bit by 128-bit unsigned division and remainder with carry tracking so rem never overflows.
void Int128::udiv128Full(uint64_t nlo, uint64_t nhi, uint64_t dlo, uint64_t dhi,
                         uint64_t* qlo, uint64_t* qhi, uint64_t* rlo, uint64_t* rhi) {
    if (dlo == 0 && dhi == 0) {
        *qlo = *qhi = *rlo = *rhi = 0;
        return;
    }
    if (nhi == 0 && dhi == 0) {
        *qlo = nlo / dlo;
        *qhi = 0;
        *rlo = nlo % dlo;
        *rhi = 0;
        return;
    }
    uint64_t remLo = 0, remHi = 0, qLoVal = 0, qHiVal = 0;
    for (int i = 127; i >= 0; --i) {
        const uint64_t bit = (i >= 64) ? ((nhi >> (i - 64)) & 1ULL) : ((nlo >> i) & 1ULL);
        const uint64_t carry = (remHi >> 63) & 1ULL;
        remHi = (remHi << 1) | (remLo >> 63);
        remLo = (remLo << 1) | bit;
        const bool ge = (carry != 0) || (remHi > dhi) || (remHi == dhi && remLo >= dlo);
        if (ge) {
            const uint64_t oldLo = remLo;
            remLo -= dlo;
            remHi = remHi - dhi - (oldLo < dlo ? 1ULL : 0ULL);
            if (i >= 64)
                qHiVal |= (1ULL << (i - 64));
            else
                qLoVal |= (1ULL << i);
        }
    }
    *qlo = qLoVal;
    *qhi = qHiVal;
    *rlo = remLo;
    *rhi = remHi;
}

Int128 Int128::divRem(const Int128& num, const Int128& den, Int128* rem) {
    if (den == Int128(0)) {
        if (rem) *rem = Int128(0);
        return Int128(0);
    }
    const bool negQ = num.negative() != den.negative();
    const bool negR = num.negative();
    uint64_t nlo = 0, nhi = 0, dlo = 0, dhi = 0;
    absU128(num, nlo, nhi);
    absU128(den, dlo, dhi);
    uint64_t qlo = 0, qhi = 0, rlo = 0, rhi = 0;
    udiv128Full(nlo, nhi, dlo, dhi, &qlo, &qhi, &rlo, &rhi);
    if (rem) *rem = fromAbsU128(rlo, rhi, negR);
    return fromAbsU128(qlo, qhi, negQ);
}

Int128 Int128::divEuclid(const Int128& num, const Int128& den) {
    // after Rust i128::div_euclid (truncating / and %, then step down if r < 0)
    Int128 rem;
    Int128 q = divRem(num, den, &rem);
    if (rem.negative()) {
        if (den.negative())
            q = q + Int128(1);
        else
            q = q - Int128(1);
    }
    return q;
}

Int128 Int128::operator/(int64_t d) const {
    if (d == 0) return Int128(0);
    return divRem(*this, Int128(d), nullptr);
}

int64_t Int128::saturatingToI64() const {
    if (hi > 0 || (hi == 0 && lo > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))) return std::numeric_limits<int64_t>::max();
    if (hi < -1 || (hi == -1 && lo < 0x8000000000000000ULL)) return std::numeric_limits<int64_t>::min();
    return static_cast<int64_t>(lo);
}

} // namespace Time
