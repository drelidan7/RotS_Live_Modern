// weather_tests.cpp

// New test TU (LS-1 Wave Task 2, world batch coverage rider -- census's
// Step 7 explicit flag: "world/weather.cpp -- weather/MSDP broadcast has
// some coverage from the world-seed wave; confirm the &world[roomnum]
// conversions are hit, else add a rider"). get_sun_level() (weather.cpp)
// had zero test coverage anywhere in the tree before this task converted
// its six world[room]/&world[...] reads to room_by_id_total(room) calls
// (ls1-task-2-report.md, world batch); the MSDP weather-string tests in
// protocol_tests.cpp exercise a *different* function (broadcast_weather_msdp_update
// / weather_message_for_room), never get_sun_level itself. These tests
// exercise every branch of get_sun_level() through its converted
// room_by_id_total(room)->room_flags/->sector_type reads, using the shared
// ScopedTestWorld fixture (test_world.h) for the room and a small
// file-local RAII for weather_info (a plain global weather.cpp defines;
// not touched by any other test file's fixture, so no shared teardown
// exists to reuse).

#include "rots/core/types.h"
#include "test_world.h"

#include <gtest/gtest.h>

// get_sun_level() (weather.cpp) has no header declaration anywhere in the
// tree; forward-declared locally, matching combat/limits.cpp's own
// file-local forward declaration of the same function.
int get_sun_level(int room);

extern struct weather_data weather_info;

namespace {

// Saves and restores the three weather_info fields get_sun_level() reads
// (sunlight, plus snow[sector]/sky[sector] for the one sector a test
// exercises) so no test's weather state leaks into a later test sharing
// this process's global weather_info.
class ScopedWeatherState {
public:
    explicit ScopedWeatherState(int sector)
        : sector_(sector)
        , sunlight_(weather_info.sunlight)
        , snow_(weather_info.snow[sector])
        , sky_(weather_info.sky[sector])
    {
    }

    ~ScopedWeatherState()
    {
        weather_info.sunlight = sunlight_;
        weather_info.snow[sector_] = snow_;
        weather_info.sky[sector_] = sky_;
    }

private:
    // Sector index this instance's snow_/sky_ apply to -- passed back to
    // weather_info.snow[]/sky[] on restore.
    int sector_;
    // Pre-test weather_info.sunlight, restored on scope exit.
    int sunlight_;
    // Pre-test weather_info.snow[sector_], restored on scope exit.
    int snow_;
    // Pre-test weather_info.sky[sector_], restored on scope exit.
    int sky_;
};

} // namespace

TEST(GetSunLevel, ReturnsZeroWhenSunlightIsDark)
{
    ScopedTestWorld test_world;
    ScopedWeatherState weather_state(SECT_FIELD);

    test_world.room().room_flags = 0;
    test_world.room().sector_type = SECT_FIELD;
    weather_info.sunlight = SUN_DARK;

    EXPECT_EQ(get_sun_level(0), 0);
}

TEST(GetSunLevel, ReturnsZeroWhenRoomIsFlaggedDark)
{
    ScopedTestWorld test_world;
    ScopedWeatherState weather_state(SECT_FIELD);

    test_world.room().room_flags = DARK;
    test_world.room().sector_type = SECT_FIELD;
    weather_info.sunlight = SUN_LIGHT;

    EXPECT_EQ(get_sun_level(0), 0);
}

TEST(GetSunLevel, ReturnsZeroWhenSectorIsInside)
{
    ScopedTestWorld test_world;
    ScopedWeatherState weather_state(SECT_INSIDE);

    test_world.room().room_flags = 0;
    test_world.room().sector_type = SECT_INSIDE;
    weather_info.sunlight = SUN_LIGHT;

    EXPECT_EQ(get_sun_level(0), 0);
}

TEST(GetSunLevel, ComputesBaseLevelForFieldSectorWithNoModifiers)
{
    ScopedTestWorld test_world;
    ScopedWeatherState weather_state(SECT_FIELD);

    test_world.room().room_flags = 0;
    test_world.room().sector_type = SECT_FIELD;
    weather_info.sunlight = SUN_LIGHT;
    weather_info.sky[SECT_FIELD] = SKY_CLOUDY;
    weather_info.snow[SECT_FIELD] = 0;

    EXPECT_EQ(get_sun_level(0), 10);
}

TEST(GetSunLevel, AddsSnowAndCloudlessBonuses)
{
    ScopedTestWorld test_world;
    ScopedWeatherState weather_state(SECT_FIELD);

    test_world.room().room_flags = 0;
    test_world.room().sector_type = SECT_FIELD;
    weather_info.sunlight = SUN_LIGHT;
    weather_info.sky[SECT_FIELD] = SKY_CLOUDLESS;
    weather_info.snow[SECT_FIELD] = 1;

    // 10 base (SECT_FIELD) + 4 snow bonus + 2 cloudless bonus.
    EXPECT_EQ(get_sun_level(0), 16);
}

TEST(GetSunLevel, SubtractsOneWhenSkyIsWorseThanCloudy)
{
    ScopedTestWorld test_world;
    ScopedWeatherState weather_state(SECT_FIELD);

    test_world.room().room_flags = 0;
    test_world.room().sector_type = SECT_FIELD;
    weather_info.sunlight = SUN_LIGHT;
    weather_info.sky[SECT_FIELD] = SKY_RAINING;
    weather_info.snow[SECT_FIELD] = 0;

    // 10 base (SECT_FIELD) - 1 sky-worse-than-cloudy penalty.
    EXPECT_EQ(get_sun_level(0), 9);
}

TEST(GetSunLevel, HalvesForShadowyRoomFlag)
{
    ScopedTestWorld test_world;
    ScopedWeatherState weather_state(SECT_FIELD);

    test_world.room().room_flags = SHADOWY;
    test_world.room().sector_type = SECT_FIELD;
    weather_info.sunlight = SUN_LIGHT;
    weather_info.sky[SECT_FIELD] = SKY_CLOUDY;
    weather_info.snow[SECT_FIELD] = 0;

    // 10 base (SECT_FIELD), halved by the SHADOWY room flag.
    EXPECT_EQ(get_sun_level(0), 5);
}

TEST(GetSunLevel, HalvesAtSunriseAndSunset)
{
    ScopedTestWorld test_world;
    ScopedWeatherState weather_state(SECT_FIELD);

    test_world.room().room_flags = 0;
    test_world.room().sector_type = SECT_FIELD;
    weather_info.sky[SECT_FIELD] = SKY_CLOUDY;
    weather_info.snow[SECT_FIELD] = 0;

    weather_info.sunlight = SUN_RISE;
    EXPECT_EQ(get_sun_level(0), 5);

    weather_info.sunlight = SUN_SET;
    EXPECT_EQ(get_sun_level(0), 5);
}
