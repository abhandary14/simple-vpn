// Crypto round-trip and tamper-detection tests (FR-8.3, TDD.md section 10).

#include <cassert>
#include <cstdio>
#include <cstring>

#include "crypto.h"

static void test_roundtrip()
{
    uint8_t key[KEY_LEN];
    for (size_t i = 0; i < KEY_LEN; i++)
        key[i] = static_cast<uint8_t>(i);

    const char *msg = "the quick brown fox jumps over the lazy dog";
    size_t pt_len = std::strlen(msg);

    uint8_t nonce[NONCE_LEN];
    uint8_t tag[TAG_LEN];
    uint8_t ciphertext[256];
    uint8_t plaintext[256];

    bool enc_ok = aead_encrypt(key, reinterpret_cast<const uint8_t *>(msg), pt_len,
                               nonce, tag, ciphertext);
    assert(enc_ok);

    bool dec_ok = aead_decrypt(key, nonce, tag, ciphertext, pt_len, plaintext);
    assert(dec_ok);
    assert(std::memcmp(plaintext, msg, pt_len) == 0);

    std::printf("test_roundtrip: PASS\n");
}

static void test_tamper_detection()
{
    uint8_t key[KEY_LEN];
    for (size_t i = 0; i < KEY_LEN; i++)
        key[i] = static_cast<uint8_t>(KEY_LEN - i);

    const char *msg = "tamper with me";
    size_t pt_len = std::strlen(msg);

    uint8_t nonce[NONCE_LEN];
    uint8_t tag[TAG_LEN];
    uint8_t ciphertext[256];
    uint8_t plaintext[256];

    assert(aead_encrypt(key, reinterpret_cast<const uint8_t *>(msg), pt_len, nonce, tag, ciphertext));

    // Flip a bit in the ciphertext.
    ciphertext[0] ^= 0x01;
    assert(!aead_decrypt(key, nonce, tag, ciphertext, pt_len, plaintext));

    // Restore ciphertext, flip a bit in the tag instead.
    ciphertext[0] ^= 0x01;
    tag[0] ^= 0x01;
    assert(!aead_decrypt(key, nonce, tag, ciphertext, pt_len, plaintext));

    std::printf("test_tamper_detection: PASS\n");
}

int main()
{
    test_roundtrip();
    test_tamper_detection();
    std::printf("All crypto tests passed.\n");
    return 0;
}