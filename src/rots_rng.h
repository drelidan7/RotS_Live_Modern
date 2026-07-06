#pragma once

// Owned, seedable PRNG for all game randomness. std::mt19937's sequence is
// fully specified by the C++ standard, so a given seed produces identical
// draws on every platform/compiler — the property the characterization
// goldens depend on. Replaces std::rand()/random(), whose sequences are
// libc-specific.
namespace rots_rng {
void seed(unsigned int seed_value);
unsigned int next();
}
