#pragma once

// Owned, seedable PRNG for all game randomness. std::mt19937's sequence is
// fully specified by the C++ standard, so a given seed produces identical
// draws on every platform/compiler — the property the characterization
// goldens depend on. Replaces std::rand()/random(), whose sequences are
// libc-specific.
namespace rots_rng {
void seed(unsigned int seed_value);
unsigned int next();

// Seeds the engine from ROTS_RNG_SEED (decimal) when set and well-formed, for
// the combat smoke harness and future deterministic replays; otherwise falls
// back to the historical time(0) seeding, byte-identical default behavior.
void seed_from_environment_or_time();
}
