#pragma once

#include <cstdint>

namespace ds {

// PCG32 — the only RNG in the project. Same seed, same stream, same results;
// dungeon generation determinism depends on nobody using anything else.
class Rng {
public:
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t seq = 0xda3e39cb94b95bdbULL) {
        state_ = 0u;
        inc_ = (seq << 1u) | 1u;
        next_u32();
        state_ += seed;
        next_u32();
    }

    uint32_t next_u32() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // Inclusive range. Modulo bias is irrelevant at gameplay scales.
    int range_int(int lo, int hi) {
        if (hi <= lo) {
            return lo;
        }
        const auto span = static_cast<uint32_t>(hi - lo + 1);
        return lo + static_cast<int>(next_u32() % span);
    }

    float next_float01() { // [0, 1)
        return static_cast<float>(next_u32() >> 8) * (1.0f / 16777216.0f);
    }

    bool chance(float p) { return next_float01() < p; }

private:
    uint64_t state_;
    uint64_t inc_;
};

} // namespace ds
