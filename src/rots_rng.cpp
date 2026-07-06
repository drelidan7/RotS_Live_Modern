#include "rots_rng.h"

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

}
