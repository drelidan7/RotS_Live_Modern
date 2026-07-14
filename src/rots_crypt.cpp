#include "rots_crypt.h"

#include "text_view.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

// ---- SHA-512 core (FIPS 180-4) --------------------------------------------

constexpr int kShaBlockBytes = 128;
constexpr int kShaDigestBytes = 64;

constexpr std::uint64_t kInitialHash[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

constexpr std::uint64_t kRoundConstants[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

std::uint64_t rotr64(std::uint64_t value, int bits)
{
    return (value >> bits) | (value << (64 - bits));
}

// Folds exactly one 128-byte block into `state` (FIPS 180-4 section 6.4.2).
void sha512_process_block(std::uint64_t state[8], const unsigned char block[kShaBlockBytes])
{
    std::uint64_t schedule[80];
    for (int t = 0; t < 16; ++t) {
        std::uint64_t word = 0;
        for (int byte_index = 0; byte_index < 8; ++byte_index)
            word = (word << 8) | block[t * 8 + byte_index];
        schedule[t] = word;
    }
    for (int t = 16; t < 80; ++t) {
        std::uint64_t s0 = rotr64(schedule[t - 15], 1) ^ rotr64(schedule[t - 15], 8) ^ (schedule[t - 15] >> 7);
        std::uint64_t s1 = rotr64(schedule[t - 2], 19) ^ rotr64(schedule[t - 2], 61) ^ (schedule[t - 2] >> 6);
        schedule[t] = schedule[t - 16] + s0 + schedule[t - 7] + s1;
    }

    std::uint64_t a = state[0];
    std::uint64_t b = state[1];
    std::uint64_t c = state[2];
    std::uint64_t d = state[3];
    std::uint64_t e = state[4];
    std::uint64_t f = state[5];
    std::uint64_t g = state[6];
    std::uint64_t h = state[7];

    for (int t = 0; t < 80; ++t) {
        std::uint64_t big_s1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        std::uint64_t choice = (e & f) ^ (~e & g);
        std::uint64_t temp1 = h + big_s1 + choice + kRoundConstants[t] + schedule[t];
        std::uint64_t big_s0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        std::uint64_t majority = (a & b) ^ (a & c) ^ (b & c);
        std::uint64_t temp2 = big_s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

// Incremental SHA-512 digest. Role: the Drepper crypt algorithm below builds
// several digests out of interleaved, non-block-aligned process() calls
// (password, salt, password again, cycles of a prior digest, ...), so a
// single one-shot hash function isn't enough — this mirrors glibc's
// sha512_ctx streaming API.
class Sha512Context
{
public:
    Sha512Context() { std::memcpy(state_, kInitialHash, sizeof(state_)); }

    void process(const void* data, size_t length)
    {
        total_bytes_ += length;
        append_bytes(static_cast<const unsigned char*>(data), length);
    }

    // Writes the 64-byte digest to `digest_out`. The context must not be
    // reused afterward (matches glibc's sha512_finish_ctx contract).
    void finish(unsigned char digest_out[kShaDigestBytes])
    {
        const std::uint64_t total_bits = total_bytes_ * 8;

        // FIPS 180-4 padding: a single 0x80 bit, then zeros, until the
        // buffer holds exactly 112 bytes (mod 128) so the last 16 bytes of
        // the final block are free for the bit-length suffix below.
        unsigned char pad[kShaBlockBytes] = { 0 };
        pad[0] = 0x80;
        const size_t pad_len = (buffer_len_ < static_cast<size_t>(kShaBlockBytes - 16))
            ? (kShaBlockBytes - 16 - buffer_len_)
            : (2 * kShaBlockBytes - 16 - buffer_len_);
        append_bytes(pad, pad_len);

        unsigned char length_field[16] = { 0 };
        for (int i = 0; i < 8; ++i)
            length_field[15 - i] = static_cast<unsigned char>(total_bits >> (8 * i));
        append_bytes(length_field, sizeof(length_field));

        for (int word_index = 0; word_index < 8; ++word_index)
            for (int byte_index = 0; byte_index < 8; ++byte_index)
                digest_out[word_index * 8 + byte_index] = static_cast<unsigned char>(state_[word_index] >> (56 - 8 * byte_index));
    }

    // Best-effort clearing of hash-state working memory that transiently holds
    // key/salt-derived material, on every exit path from every Sha512Context
    // instance in rots_crypt() below (parked Phase 2b finding, addressed here per
    // Phase 3 Task 5's crypt-scrubbing item). std::fill on plain memory is not a
    // dedicated secure-zero primitive -- an aggressive optimizer is, in principle,
    // still free to treat a write to memory about to go out of scope as dead-store
    // elimination, so this is defense-in-depth rather than a cryptographic
    // guarantee. A proper explicit_bzero()/SecureZeroMemory()-based helper (neither
    // of which is portable across POSIX and MSVC without its own shim) is Phase 5
    // hardening work.
    ~Sha512Context()
    {
        std::fill(state_, state_ + 8, std::uint64_t { 0 });
        std::fill(buffer_, buffer_ + kShaBlockBytes, static_cast<unsigned char>(0));
        buffer_len_ = 0;
        total_bytes_ = 0;
    }

private:
    // Appends raw bytes to the block buffer, flushing full 128-byte blocks
    // into `state_` as they fill. Unlike process(), does not touch
    // total_bytes_ — finish() uses this directly for padding/length bytes,
    // which must never count toward the encoded message length.
    void append_bytes(const unsigned char* bytes, size_t length)
    {
        if (length == 0) {
            return;
        }

        if (buffer_len_ > 0) {
            size_t fill = kShaBlockBytes - buffer_len_;
            if (fill > length)
                fill = length;
            std::memcpy(buffer_ + buffer_len_, bytes, fill);
            buffer_len_ += fill;
            bytes += fill;
            length -= fill;
            if (buffer_len_ == kShaBlockBytes) {
                sha512_process_block(state_, buffer_);
                buffer_len_ = 0;
            }
        }

        while (length >= static_cast<size_t>(kShaBlockBytes)) {
            sha512_process_block(state_, bytes);
            bytes += kShaBlockBytes;
            length -= kShaBlockBytes;
        }

        if (length > 0) {
            std::memcpy(buffer_, bytes, length);
            buffer_len_ = length;
        }
    }

    std::uint64_t state_[8]; // Running SHA-512 hash state (H0..H7); updated one 128-byte block at a time.
    unsigned char buffer_[kShaBlockBytes]; // Holds a not-yet-full block between process()/append_bytes() calls.
    size_t buffer_len_ = 0; // Number of valid bytes currently sitting in buffer_.
    std::uint64_t total_bytes_ = 0; // Total message bytes passed to process() so far; encoded as the FIPS length suffix in finish().
};

// ---- SHA-512-crypt ($6$) — Drepper's public-domain reference algorithm ----
// https://www.akkadia.org/drepper/SHA-crypt.txt

constexpr char kSha512CryptPrefix[] = "$6$";
constexpr char kRoundsPrefix[] = "rounds=";
constexpr size_t kSaltLenMax = 16;
constexpr unsigned long kRoundsDefault = 5000;
constexpr unsigned long kRoundsMin = 1000;
constexpr unsigned long kRoundsMax = 999999999UL;
constexpr char kBase64Alphabet[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

// Appends `digit_count` base64 digits (least-significant-first) encoding the
// 24-bit value (b2<<16)|(b1<<8)|b0 — Drepper's b64_from_24bit() macro.
void append_base64_24bit(std::string& out, unsigned char b2, unsigned char b1, unsigned char b0, int digit_count)
{
    unsigned int word = (static_cast<unsigned int>(b2) << 16) | (static_cast<unsigned int>(b1) << 8) | b0;
    for (int i = 0; i < digit_count; ++i) {
        out += kBase64Alphabet[word & 0x3f];
        word >>= 6;
    }
}

// Cycles `digest` (kShaDigestBytes long) into `out[0..out_len)`, repeating
// the digest as many whole/partial copies as needed — the shared shape of
// the P-byte and S-byte sequence construction (steps 13-20 of the spec).
void cycle_digest_into(std::string& out, size_t out_len, const unsigned char* digest)
{
    out.resize(out_len);
    size_t offset = 0;
    while (offset + kShaDigestBytes <= out_len) {
        std::memcpy(&out[offset], digest, kShaDigestBytes);
        offset += kShaDigestBytes;
    }
    if (offset < out_len)
        std::memcpy(&out[offset], digest, out_len - offset);
}

// Result buffer for rots_crypt(). Role: mirrors libc crypt()'s own
// static-buffer contract — overwritten by the next call. Safe because the
// game server is single-threaded (see rots_crypt.h).
std::string g_result_buffer;

} // namespace

const char* rots_crypt(const char* key, const char* setting)
{
    if (key == nullptr || setting == nullptr)
        return nullptr;

    return rots_crypt(std::string_view(key), std::string_view(setting));
}

const char* rots_crypt(std::string_view key, std::string_view setting)
{
    key = rots::text::truncate_at_null(key);
    setting = rots::text::truncate_at_null(setting);

    constexpr std::string_view sha512_crypt_prefix(kSha512CryptPrefix, sizeof(kSha512CryptPrefix) - 1);
    constexpr std::string_view rounds_prefix(kRoundsPrefix, sizeof(kRoundsPrefix) - 1);
    if (!setting.starts_with(sha512_crypt_prefix))
        return nullptr;
    size_t cursor = sha512_crypt_prefix.size();

    unsigned long rounds = kRoundsDefault;
    bool rounds_custom = false;
    if (setting.substr(cursor).starts_with(rounds_prefix)) {
        // A malformed "rounds=" spec REJECTS the whole setting rather than
        // clamping or falling back: empty/non-numeric digits, a missing '$'
        // terminator, and values outside [kRoundsMin, kRoundsMax] all return
        // nullptr. This matches the project's reference implementation —
        // libxcrypt (Debian's libcrypt, which produced every existing stored
        // hash), verified by in-container probes: crypt() returns its "*0"
        // failure token for "$6$rounds=$...", "$6$rounds=abc$...",
        // "$6$rounds=999$...", and "$6$rounds=1000000000$...". (Historical
        // glibc clamped out-of-range values instead; stored hashes only ever
        // echo canonical in-range values, so the difference is unreachable
        // through real account data.)
        cursor += rounds_prefix.size();
        if (cursor >= setting.size() || !std::isdigit(static_cast<unsigned char>(setting[cursor])))
            return nullptr;

        unsigned long parsed_rounds = 0;
        while (cursor < setting.size() && std::isdigit(static_cast<unsigned char>(setting[cursor]))) {
            const unsigned long digit = static_cast<unsigned long>(setting[cursor] - '0');
            if (parsed_rounds > (kRoundsMax - digit) / 10)
                return nullptr;
            parsed_rounds = parsed_rounds * 10 + digit;
            ++cursor;
        }
        if (cursor >= setting.size() || setting[cursor] != '$'
            || parsed_rounds < kRoundsMin || parsed_rounds > kRoundsMax)
            return nullptr;
        ++cursor;
        rounds = parsed_rounds;
        rounds_custom = true;
    }

    const size_t salt_delimiter = setting.find('$', cursor);
    const size_t available_salt_length = salt_delimiter == std::string_view::npos
        ? setting.size() - cursor
        : salt_delimiter - cursor;
    const size_t salt_len = std::min(kSaltLenMax, available_salt_length);
    const std::string_view salt = setting.substr(cursor, salt_len);

    const size_t key_len = key.size();
    const unsigned char* key_bytes = reinterpret_cast<const unsigned char*>(key.data());
    const unsigned char* salt_bytes = reinterpret_cast<const unsigned char*>(salt.data());

    // Step 4-8: digest B = SHA512(key + salt + key).
    unsigned char alt_result[kShaDigestBytes];
    {
        Sha512Context alt_ctx;
        alt_ctx.process(key_bytes, key_len);
        alt_ctx.process(salt_bytes, salt_len);
        alt_ctx.process(key_bytes, key_len);
        alt_ctx.finish(alt_result);
    }

    // Step 1-3, 9-12: digest A = key + salt + (digest B cycled to key_len
    // bytes) + (per length-bit: digest B if 1, key if 0).
    Sha512Context ctx;
    ctx.process(key_bytes, key_len);
    ctx.process(salt_bytes, salt_len);

    {
        size_t remaining = key_len;
        while (remaining > static_cast<size_t>(kShaDigestBytes)) {
            ctx.process(alt_result, kShaDigestBytes);
            remaining -= kShaDigestBytes;
        }
        ctx.process(alt_result, remaining);
    }

    for (size_t count = key_len; count > 0; count >>= 1) {
        if ((count & 1) != 0)
            ctx.process(alt_result, kShaDigestBytes);
        else
            ctx.process(key_bytes, key_len);
    }

    ctx.finish(alt_result); // alt_result now holds digest A.

    // Step 13-16: P sequence = SHA512(key repeated key_len times), cycled to key_len bytes.
    std::string p_bytes;
    if (key_len > 0) {
        unsigned char temp_result[kShaDigestBytes];
        Sha512Context p_ctx;
        for (size_t i = 0; i < key_len; ++i)
            p_ctx.process(key_bytes, key_len);
        p_ctx.finish(temp_result);
        cycle_digest_into(p_bytes, key_len, temp_result);
    }

    // Step 17-20: S sequence = SHA512(salt repeated (16 + alt_result[0]) times), cycled to salt_len bytes.
    std::string s_bytes;
    if (salt_len > 0) {
        unsigned char temp_result[kShaDigestBytes];
        Sha512Context s_ctx;
        const size_t repeat_count = 16 + alt_result[0];
        for (size_t i = 0; i < repeat_count; ++i)
            s_ctx.process(salt_bytes, salt_len);
        s_ctx.finish(temp_result);
        cycle_digest_into(s_bytes, salt_len, temp_result);
    }

    // Step 21: main stretching loop.
    const unsigned char* p_ptr = reinterpret_cast<const unsigned char*>(p_bytes.data());
    const unsigned char* s_ptr = reinterpret_cast<const unsigned char*>(s_bytes.data());
    for (unsigned long round = 0; round < rounds; ++round) {
        Sha512Context round_ctx;

        if ((round & 1) != 0)
            round_ctx.process(p_ptr, key_len);
        else
            round_ctx.process(alt_result, kShaDigestBytes);

        if (round % 3 != 0)
            round_ctx.process(s_ptr, salt_len);

        if (round % 7 != 0)
            round_ctx.process(p_ptr, key_len);

        if ((round & 1) != 0)
            round_ctx.process(alt_result, kShaDigestBytes);
        else
            round_ctx.process(p_ptr, key_len);

        round_ctx.finish(alt_result);
    }

    // Step 22: encode "$6$[rounds=N$]salt$<86 base64 chars>".
    std::string& out = g_result_buffer;
    out.clear();
    out += kSha512CryptPrefix;
    if (rounds_custom) {
        out += kRoundsPrefix;
        out += std::to_string(rounds);
        out += '$';
    }
    out += salt;
    out += '$';

    append_base64_24bit(out, alt_result[0], alt_result[21], alt_result[42], 4);
    append_base64_24bit(out, alt_result[22], alt_result[43], alt_result[1], 4);
    append_base64_24bit(out, alt_result[44], alt_result[2], alt_result[23], 4);
    append_base64_24bit(out, alt_result[3], alt_result[24], alt_result[45], 4);
    append_base64_24bit(out, alt_result[25], alt_result[46], alt_result[4], 4);
    append_base64_24bit(out, alt_result[47], alt_result[5], alt_result[26], 4);
    append_base64_24bit(out, alt_result[6], alt_result[27], alt_result[48], 4);
    append_base64_24bit(out, alt_result[28], alt_result[49], alt_result[7], 4);
    append_base64_24bit(out, alt_result[50], alt_result[8], alt_result[29], 4);
    append_base64_24bit(out, alt_result[9], alt_result[30], alt_result[51], 4);
    append_base64_24bit(out, alt_result[31], alt_result[52], alt_result[10], 4);
    append_base64_24bit(out, alt_result[53], alt_result[11], alt_result[32], 4);
    append_base64_24bit(out, alt_result[12], alt_result[33], alt_result[54], 4);
    append_base64_24bit(out, alt_result[34], alt_result[55], alt_result[13], 4);
    append_base64_24bit(out, alt_result[56], alt_result[14], alt_result[35], 4);
    append_base64_24bit(out, alt_result[15], alt_result[36], alt_result[57], 4);
    append_base64_24bit(out, alt_result[37], alt_result[58], alt_result[16], 4);
    append_base64_24bit(out, alt_result[59], alt_result[17], alt_result[38], 4);
    append_base64_24bit(out, alt_result[18], alt_result[39], alt_result[60], 4);
    append_base64_24bit(out, alt_result[40], alt_result[61], alt_result[19], 4);
    append_base64_24bit(out, alt_result[62], alt_result[20], alt_result[41], 4);
    append_base64_24bit(out, 0, 0, alt_result[63], 2);

    // Zeroize the working buffers that held key/salt-derived intermediate digest
    // material now that they have been fully consumed into `out` -- `out` itself
    // (g_result_buffer) is the return value and must survive past this point, so it
    // is deliberately left untouched. Same caveat as the Sha512Context destructor
    // above: std::fill is best-effort, not a dedicated secure-zero primitive.
    std::fill(alt_result, alt_result + kShaDigestBytes, static_cast<unsigned char>(0));
    std::fill(p_bytes.begin(), p_bytes.end(), '\0');
    std::fill(s_bytes.begin(), s_bytes.end(), '\0');

    return out.c_str();
}
