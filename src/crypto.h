#pragma once

#include <cstddef>
#include <cstdint>

#include "common.h"

// Encrypts `plaintext` (pt_len bytes) under `key` using AES-256-GCM.
// Generates a random 12-byte nonce internally (via RAND_bytes) and writes it
// to out_nonce. Writes the 16-byte GCM authentication tag to out_tag and the
// ciphertext (pt_len bytes) to out_ciphertext.
// Returns true on success.
bool aead_encrypt(const uint8_t key[KEY_LEN],
                   const uint8_t* plaintext, size_t pt_len,
                   uint8_t out_nonce[NONCE_LEN],
                   uint8_t out_tag[TAG_LEN],
                   uint8_t* out_ciphertext);

// Decrypts `ciphertext` (ct_len bytes) under `key`, `nonce`, and `tag`.
// Writes ct_len bytes of plaintext to out_plaintext.
// Returns false if GCM tag verification fails (out_plaintext is undefined in
// that case).
bool aead_decrypt(const uint8_t key[KEY_LEN],
                   const uint8_t nonce[NONCE_LEN],
                   const uint8_t tag[TAG_LEN],
                   const uint8_t* ciphertext, size_t ct_len,
                   uint8_t* out_plaintext);

// Loads exactly KEY_LEN bytes from `path` into `out_key`.
// Returns false (with an [error] message on stderr) if the file size is not
// KEY_LEN or the file cannot be read.
// Prints a [warn] to stderr (but still returns true) if the file's
// permission bits grant access to group or other.
bool load_key(const char* path, uint8_t out_key[KEY_LEN]);