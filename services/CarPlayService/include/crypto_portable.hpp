// Dependency-free SHA-512 and AES-128-CTR (the NDK exposes no libcrypto). Cross-checked against OpenSSL in
// tests/test_crypto_xcheck.cpp and against the OrangeCar golden key/IV vector.
// Not constant-time: used only for the legacy et=1 CarPlay video stream whose key is derived from a value sent in
// the clear, so there is no secret to leak by timing. Do NOT reuse for real secrets.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace s10::crypto {

std::array<uint8_t, 64> sha512(const uint8_t* d, size_t n);

class Aes128Ctr {
public:
    // Big-endian 128-bit counter starting at iv; keystream continuous across process() calls.
    Aes128Ctr(const uint8_t key[16], const uint8_t iv[16]);
    void process(const uint8_t* in, uint8_t* out, size_t n);
    // Single-block ECB encrypt (exposed for known-answer tests, FIPS-197).
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) const;
private:
    void refill();
    uint8_t rk_[176];
    uint8_t ctr_[16];
    uint8_t ks_[16];
    size_t used_ = 16;
};

}  // namespace s10::crypto
