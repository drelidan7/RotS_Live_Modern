#include "../account_management.h"
#include "../rots_crypt.h"

#include <gtest/gtest.h>

#include <cstring>

// Reference vectors were captured directly from crypt() in the project's
// i386 container (Debian 11; libcrypt is libxcrypt 4.4.18 — the library that
// produced every existing stored account hash):
//
//   #define _GNU_SOURCE
//   #include <crypt.h>
//   crypt("password123", "$6$abcdefghijklmnop$");
//   crypt("Tr0ub4dor&3", "$6$rounds=10000$saltsalt12345678$");
//   crypt("hunter2", "$6$abcd$");
//   crypt(<130 x 'a'..'z' repeating>, "$6$longkeysalt16ch$");
//
// The two "Hello world!" vectors are Ulrich Drepper's published reference
// vectors from the SHA-crypt spec
// (https://www.akkadia.org/drepper/SHA-crypt.txt); they were re-verified
// against the same in-container crypt() before being trusted here, matching
// byte-for-byte.

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

// Exercises Sha512Context's multi-block bulk path: a 130-character password
// exceeds the 128-byte SHA-512 block, so every process(key, key_len) call
// crosses a block boundary and the P-sequence construction (key repeated 130
// times = 16,900 bytes) streams through the `while (length >= kShaBlockBytes)`
// loop that no short-password vector reaches. Reference output captured from
// the in-container crypt() with the same 130-char input.
TEST(RotsCrypt, MatchesGlibcVectorMultiBlockPassword)
{
    std::string long_password;
    long_password.reserve(130);
    for (int i = 0; i < 130; ++i)
        long_password += static_cast<char>('a' + (i % 26));

    const char* result = rots_crypt(long_password.c_str(), "$6$longkeysalt16ch$");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result,
        "$6$longkeysalt16ch$VS24p3IpC0d1VwWh1kjP5ofogbvsricGp/CjZj.OYZMCSp0Hi14T8.NKY1e/HduJy6Zns/JaoFeDqe4oyfOnG1");
}

TEST(RotsCrypt, NonSha512SettingReturnsNull)
{
    EXPECT_EQ(rots_crypt("whatever", "$1$abcdefgh$"), nullptr);
    EXPECT_EQ(rots_crypt("whatever", "plainDESsalt"), nullptr);
}

// The reference libcrypt (libxcrypt — Debian's libcrypt, which produced every
// existing stored hash) REJECTS malformed or out-of-range "rounds=" specs
// rather than clamping: in-container probes show crypt() returning its "*0"
// failure token for each of these settings. rots_crypt mirrors that as
// nullptr. (Historical glibc clamped out-of-range values instead; stored
// hashes only ever echo canonical in-range values, so the difference cannot
// surface through real account data.)
TEST(RotsCrypt, MalformedOrOutOfRangeRoundsReturnNull)
{
    EXPECT_EQ(rots_crypt("password123", "$6$rounds=$abcdefghijklmnop$"), nullptr);
    EXPECT_EQ(rots_crypt("password123", "$6$rounds=abc$saltsalt$"), nullptr);
    EXPECT_EQ(rots_crypt("password123", "$6$rounds=999$abcdefghijklmnop$"), nullptr);
    EXPECT_EQ(rots_crypt("password123", "$6$rounds=1000000000$abcdefghijklmnop$"), nullptr);
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
