// rots_util_tests.cpp

// New test TU (spec-pair wave, Task 1; sp-task-1-brief.md Step 2; sp-census.md
// section 4/section 6 item 4). Covers is_number() (interpre.h:128),
// relocated verbatim from interpre.cpp to rots_util.cpp this task --
// previously untested (grep across src/tests/*.cpp for is_number found 0
// hits before this task). Declaration unchanged in interpre.h, so this file
// includes that header rather than redeclaring the symbol locally.

#include "../interpre.h"

#include <gtest/gtest.h>

TEST(IsNumber, ReturnsTrueForAllDigitString)
{
    char digits[] = "12345";
    EXPECT_NE(is_number(digits), 0);
}

TEST(IsNumber, ReturnsFalseForEmptyString)
{
    char empty[] = "";
    EXPECT_EQ(is_number(empty), 0);
}

TEST(IsNumber, ReturnsFalseWhenAnyCharacterIsNotADigit)
{
    char mixed[] = "12a45";
    EXPECT_EQ(is_number(mixed), 0);
}

TEST(IsNumber, ReturnsFalseForALeadingMinusSign)
{
    // is_number()'s digit-scan has no sign handling -- '-' fails the '0'..'9'
    // range check on the first character, same as any other non-digit.
    char negative[] = "-5";
    EXPECT_EQ(is_number(negative), 0);
}
