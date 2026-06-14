#pragma once

#include <cstddef>
#include <cstdint>

// Network defaults (TDD.md section 2)
constexpr uint16_t DEFAULT_PORT = 51820;
constexpr const char *DEFAULT_KEY_PATH = "./shared.key";
constexpr const char *DEFAULT_TUN_IP_SERVER = "10.0.0.1";
constexpr const char *DEFAULT_TUN_IP_CLIENT = "10.0.0.2";
constexpr const char *TUN_NETMASK = "255.255.255.0";
constexpr int TUN_MTU = 1400;
constexpr const char *TUN_DEVICE_NAME = "tun0";

// Buffer / crypto sizing (TDD.md section 2)
constexpr size_t BUF_SIZE = 2048;
constexpr size_t NONCE_LEN = 12;
constexpr size_t TAG_LEN = 16;
constexpr size_t KEY_LEN = 32;
constexpr size_t CRYPTO_OVERHEAD = NONCE_LEN + TAG_LEN;
