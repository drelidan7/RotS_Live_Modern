/* clock.cpp */

#include "clock.h"

//============================================================================
rots_clock::rots_clock()
    : current_time(std::chrono::steady_clock::now())
{
}

//============================================================================
float rots_clock::get_elapsed_seconds()
{
    const std::chrono::steady_clock::time_point last_time = current_time;
    current_time = std::chrono::steady_clock::now();

    // Same units/rounding as the legacy gettimeofday/timeval implementation this
    // replaced: truncate to whole microseconds (gettimeofday's native resolution)
    // before converting to a float seconds value, rather than dividing whatever
    // finer-grained duration steady_clock happens to report.
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(current_time - last_time);
    return elapsed_us.count() / 1000000.0f;
}
