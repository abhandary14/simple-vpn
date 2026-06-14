#include "crypto.h"

#include <cstdio>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

bool aead_encrypt(const uint8_t key[KEY_LEN],
                   const uint8_t* plaintext, size_t pt_len,
                   uint8_t out_nonce[NONCE_LEN],
                   uint8_t out_tag[TAG_LEN],
                   uint8_t* out_ciphertext) {
    // A fresh random nonce per packet is required for GCM safety with a
    // static key (FR-3.2).
    if (RAND_bytes(out_nonce, static_cast<int>(NONCE_LEN)) != 1) {
        std::fprintf(stderr, "[error] RAND_bytes failed\n");
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        std::fprintf(stderr, "[error] EVP_CIPHER_CTX_new failed\n");
        return false;
    }

    bool ok = true;
    int len = 0;
    int out_len = 0;

    if (ok && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) ok = false;
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(NONCE_LEN), nullptr) != 1) ok = false;
    if (ok && EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, out_nonce) != 1) ok = false;

    if (ok && pt_len > 0 &&
        EVP_EncryptUpdate(ctx, out_ciphertext, &len, plaintext, static_cast<int>(pt_len)) != 1) {
        ok = false;
    }
    if (ok) out_len = len;

    if (ok && EVP_EncryptFinal_ex(ctx, out_ciphertext + out_len, &len) != 1) ok = false;
    if (ok) out_len += len;

    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(TAG_LEN), out_tag) != 1) ok = false;

    EVP_CIPHER_CTX_free(ctx);

    if (!ok) {
        std::fprintf(stderr, "[error] AES-256-GCM encryption failed\n");
        return false;
    }
    return true;
}

bool aead_decrypt(const uint8_t key[KEY_LEN],
                   const uint8_t nonce[NONCE_LEN],
                   const uint8_t tag[TAG_LEN],
                   const uint8_t* ciphertext, size_t ct_len,
                   uint8_t* out_plaintext) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        std::fprintf(stderr, "[error] EVP_CIPHER_CTX_new failed\n");
        return false;
    }

    bool ok = true;
    int len = 0;
    int out_len = 0;

    if (ok && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) ok = false;
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(NONCE_LEN), nullptr) != 1) ok = false;
    if (ok && EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1) ok = false;

    if (ok && ct_len > 0 &&
        EVP_DecryptUpdate(ctx, out_plaintext, &len, ciphertext, static_cast<int>(ct_len)) != 1) {
        ok = false;
    }
    if (ok) out_len = len;

    // EVP_CTRL_GCM_SET_TAG takes a non-const pointer but does not modify it.
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(TAG_LEN),
                                   const_cast<uint8_t*>(tag)) != 1) {
        ok = false;
    }

    // EVP_DecryptFinal_ex returns <= 0 if the tag verification fails.
    if (ok && EVP_DecryptFinal_ex(ctx, out_plaintext + out_len, &len) <= 0) ok = false;

    EVP_CIPHER_CTX_free(ctx);

    return ok;
}

bool load_key(const char* path, uint8_t out_key[KEY_LEN]) {
    struct stat st {};
    if (stat(path, &st) != 0) {
        std::fprintf(stderr, "[error] cannot stat key file '%s': %m\n", path);
        return false;
    }
    if (st.st_size != static_cast<off_t>(KEY_LEN)) {
        std::fprintf(stderr, "[error] key file '%s' must be exactly %zu bytes (got %lld)\n",
                      path, KEY_LEN, static_cast<long long>(st.st_size));
        return false;
    }

    if (st.st_mode & (S_IRWXG | S_IRWXO)) {
        std::fprintf(stderr,
                      "[warn] key file '%s' is accessible by group/other (recommend chmod 600)\n",
                      path);
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[error] cannot open key file '%s': %m\n", path);
        return false;
    }

    size_t total = 0;
    while (total < KEY_LEN) {
        ssize_t n = read(fd, out_key + total, KEY_LEN - total);
        if (n <= 0) {
            std::fprintf(stderr, "[error] failed to read key file '%s': %m\n", path);
            close(fd);
            return false;
        }
        total += static_cast<size_t>(n);
    }

    close(fd);
    return true;
}