/* clock.h */
// Basic clock implementation to simplify time telling.

#ifndef CLOCK_H
#define CLOCK_H
#pragma once

#include <chrono>

class rots_clock {
public:
    rots_clock();

    // Returns the number of seconds that have passed since the last time this function
    // was called.
    float get_elapsed_seconds();

private:
    // Timestamp of the last measurement point: set at construction and advanced to
    // "now" on every get_elapsed_seconds() call, which measures elapsed time against
    // whatever value was stored here before the call.
    std::chrono::steady_clock::time_point current_time;
};

#endif /* CLOCK_H */
