// Cross-checks the dependency-free SHA-512 / AES-128-CTR against OpenSSL (host only) plus FIPS known answers.
#include "crypto_portable.hpp"
#include <cstdio>
#include <cstring>
#include <openssl/evp.h>
#include <random>
#include <vector>
using namespace s10::crypto;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
    // FIPS-197 appendix C.1
    const uint8_t key[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}, pt[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    const uint8_t want[16] = {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
    uint8_t iv0[16] = {0}, got[16];
    Aes128Ctr(key, iv0).encryptBlock(pt, got);
    CHECK(std::memcmp(got, want, 16) == 0);
    // FIPS 180-4 "abc"
    auto d = sha512(reinterpret_cast<const uint8_t*>("abc"), 3);
    const uint8_t abc[8] = {0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba};
    CHECK(std::memcmp(d.data(), abc, 8) == 0);

    std::mt19937_64 rng(12345);
    for (int it = 0; it < 300; ++it) {
        size_t n = rng() % 600;   // covers every padding boundary class (0, 111, 112, 127, 128, 239, 240, ...)
        if (it < 260) n = static_cast<size_t>(it);
        std::vector<uint8_t> m(n); for (auto& b : m) b = uint8_t(rng());
        unsigned char ref[64]; unsigned int rl = 0;
        EVP_Digest(m.data(), m.size(), ref, &rl, EVP_sha512(), nullptr);
        auto mine = sha512(m.data(), m.size());
        CHECK(std::memcmp(ref, mine.data(), 64) == 0);
    }
    for (int it = 0; it < 100; ++it) {
        uint8_t k[16], iv[16]; for (auto& b : k) b = uint8_t(rng()); for (auto& b : iv) b = uint8_t(rng());
        if (it % 10 == 0) std::memset(iv + 8, 0xFF, 8);                 // force counter carry across 64 bits
        if (it % 10 == 1) std::memset(iv, 0xFF, 16);                    // wrap the full 128-bit counter
        size_t n = rng() % 5000;
        std::vector<uint8_t> m(n), a(n), b(n); for (auto& x : m) x = uint8_t(rng());
        EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new(); int ol = 0;
        EVP_EncryptInit_ex(c, EVP_aes_128_ctr(), nullptr, k, iv);
        if (n) EVP_EncryptUpdate(c, a.data(), &ol, m.data(), int(n));
        EVP_CIPHER_CTX_free(c);
        Aes128Ctr mine(k, iv);
        size_t pos = 0;                                                  // random chunking: keystream must be continuous
        while (pos < n) { size_t l = std::min<size_t>(n - pos, 1 + rng() % 97); mine.process(m.data() + pos, b.data() + pos, l); pos += l; }
        CHECK(a == b);
    }
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("ALL PASSED\n");
    return 0;
}
