#include "../account_management.h"
#include "../rots_crypt.h"

#include <gtest/gtest.h>

#include <cstring>

// Reference vectors A/B/C were captured directly from glibc crypt() in the
// project's i386 container (Debian 11, glibc 2.31):
//
//   #define _GNU_SOURCE
//   #include <crypt.h>
//   crypt("password123", "$6$abcdefghijklmnop$");
//   crypt("Tr0ub4dor&3", "$6$rounds=10000$saltsalt12345678$");
//   crypt("hunter2", "$6$abcd$");
//
// Vectors D/E are Ulrich Drepper's published reference vectors from the
// SHA-crypt spec (https://www.akkadia.org/drepper/SHA-crypt.txt); they were
// re-verified against the same in-container glibc crypt() before being
// trusted here, matching byte-for-byte.

TEST(RotsCrypt, MatchesGlibcVectorImplicitRounds)
{
    const char* result = rots_crypt("password123", "$6$abcdefghijklmnop$");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result,
        "$6$abcdefghijklmnop$d7uhpfHrIKSLTOzTKRLBboNGOi5oZOHPPjFPcu09gKhzbhQta8CHqSzotrcYTTUT8uGNjizJQjyydMajlq7QV1");
}

TEST(RotsCrypt, MatchesGlibcVectorExplicitRounds)
{
    const char* result = rots_crypt("Tr0ub4dor&3", "$6$rounds=10000$saltsalt12345678$");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result,
        "$6$rounds=10000$saltsalt12345678$ECpkxnuc84dKK1z74fT6J2ARVKA5NGqrAAn7S78Nou3HkUkUtJxK5psuohYxVhUOKd8zQVIMo2aV55ylM32Ly0");
}

TEST(RotsCrypt, MatchesGlibcVectorShortSalt)
{
    const char* result = rots_crypt("hunter2", "$6$abcd$");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result,
        "$6$abcd$Qn1Z9Z5NNxb3xow2Atb3oXUiEVBwVmV4aBStoqZvjxTwRkBHzNb45iNwNBn.BoutZuVhuRGjmmU.dHMTEtO990");
}

TEST(RotsCrypt, MatchesDrepperPublishedVector)
{
    const char* result = rots_crypt("Hello world!", "$6$saltstring$");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result,
        "$6$saltstring$svn8UoSVapNtMuq1ukKS4tPQd8iKwSMHWjl/O817G3uBnIFNjnQJuesI68u4OTLiBFdcbYEdFCoEOfaS35inz1");
}

TEST(RotsCrypt, MatchesDrepperPublishedVectorWithRounds)
{
    const char* result = rots_crypt("Hello world!", "$6$rounds=10000$saltstringsaltstring$");
    ASSERT_NE(result, nullptr);
    // glibc truncates the salt to 16 characters before hashing/echoing it.
    EXPECT_STREQ(result,
        "$6$rounds=10000$saltstringsaltst$OW1/O6BYHV6BcXZu8QVeXbDWra3Oeqh0sbHbbMCVNSnCM/UrjmM0Dp8vOuZeHBy/YTBmSK6H9qs/y3RnOaw5v.");
}

TEST(RotsCrypt, NonSha512SettingReturnsNull)
{
    EXPECT_EQ(rots_crypt("whatever", "$1$abcdefgh$"), nullptr);
    EXPECT_EQ(rots_crypt("whatever", "plainDESsalt"), nullptr);
}

TEST(RotsCrypt, NullArgumentsReturnNull)
{
    EXPECT_EQ(rots_crypt(nullptr, "$6$salt$"), nullptr);
    EXPECT_EQ(rots_crypt("key", nullptr), nullptr);
}

TEST(RotsCrypt, RoundsAreEchoedOnlyWhenExplicit)
{
    const char* implicit_result = rots_crypt("password123", "$6$abcdefghijklmnop$");
    ASSERT_NE(implicit_result, nullptr);
    EXPECT_EQ(std::strstr(implicit_result, "rounds="), nullptr);

    const char* explicit_result = rots_crypt("password123", "$6$rounds=5000$abcdefghijklmnop$");
    ASSERT_NE(explicit_result, nullptr);
    EXPECT_NE(std::strstr(explicit_result, "rounds=5000$"), nullptr);
}

TEST(RotsCrypt, VerifyingAgainstFullStoredHashSucceeds)
{
    const char* hash = rots_crypt("password123", "$6$abcdefghijklmnop$");
    ASSERT_NE(hash, nullptr);
    std::string stored_hash = hash;

    // rots_crypt must accept the *full* stored hash (salt + digest) as the
    // setting, exactly like libc crypt() — it only reads through the salt.
    const char* recomputed = rots_crypt("password123", stored_hash.c_str());
    ASSERT_NE(recomputed, nullptr);
    EXPECT_STREQ(recomputed, stored_hash.c_str());
}

TEST(RotsCrypt, WrongPasswordProducesDifferentHash)
{
    const char* correct = rots_crypt("password123", "$6$abcdefghijklmnop$");
    ASSERT_NE(correct, nullptr);
    std::string correct_hash = correct;

    const char* wrong = rots_crypt("wrongpassword", correct_hash.c_str());
    ASSERT_NE(wrong, nullptr);
    EXPECT_STRNE(wrong, correct_hash.c_str());
}

// End-to-end through the real account-credential API: this is the path
// account creation/login actually exercises, not just the raw rots_crypt()
// entry point.
TEST(RotsCrypt, RoundTripsThroughAccountCredentialApi)
{
    std::string password_hash;
    std::string password_salt;
    std::string error_message;
    ASSERT_TRUE(account::generate_password_credentials("Cor7rectHorse", &password_hash, &password_salt, &error_message)) << error_message;

    EXPECT_TRUE(account::verify_password("Cor7rectHorse", password_hash));
    EXPECT_FALSE(account::verify_password("IncorrectHorse", password_hash));

    // The stored hash must be a $6$ SHA-512-crypt hash, not something else.
    EXPECT_EQ(password_hash.rfind("$6$", 0), 0u);
}
