#pragma once
// rots/core/tables.h — extern declarations of the const data tables defined in
// consts.cpp, plus the CONSTANTSMARK definition trick: consts.cpp #defines
// CONSTANTSMARK before including this header so the guarded lines below become
// the definitions in exactly one TU. Preserved verbatim from structs.h.
#include <array>
#include <string_view>

#ifdef CONSTANTSMARK
int global_release_flag = 1;
#else
extern int global_release_flag;
#endif

#ifndef CONSTANTSMARK
/// Names the seven weekdays in calendar order.
extern const std::array<std::string_view, 7> weekdays;
/// Names the seventeen months in calendar order.
extern const std::array<std::string_view, 17> month_name;
/// Names the eight moon phases in progression order.
extern const std::array<std::string_view, 8> moon_phase;
extern const std::string_view pc_races[];
extern const std::string_view pc_race_types[];
extern const std::string_view pc_race_keywords[];
extern const std::string_view pc_star_types[];
extern const std::string_view pc_named_star_types[];
#endif

