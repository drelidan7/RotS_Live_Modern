#include "rots_rng.h"

#include <cstdlib>
#include <ctime>
#include <random>

namespace {
// Single global engine: game randomness was previously one global rand()
// stream; keeping one engine preserves that structure.
std::mt19937 engine;
}

namespace rots_rng {

void seed(unsigned int seed_value)
{
    engine.seed(seed_value);
}

unsigned int next()
{
    return static_cast<unsigned int>(engine());
}

void seed_from_environment_or_time()
{
    // ROTS_RNG_SEED (decimal) pins the mt19937 stream for the combat smoke
    // harness and future deterministic replays; unset or malformed falls back
    // to the historical time(0) seeding, byte-identical default behavior.
    if (const char* env_seed = std::getenv("ROTS_RNG_SEED")) {
        char* end = nullptr;
        const unsigned long value = std::strtoul(env_seed, &end, 10);
        if (end != env_seed && *end == '\0') {
            seed(static_cast<unsigned int>(value));
            return;
        }
    }
    seed(static_cast<unsigned int>(std::time(nullptr)));
}

}
