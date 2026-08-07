# TunneLink — Technical Design Document

This document specifies the concrete technical design that implements `PRD.md`. It is the source of truth for module interfaces, wire format, and constants during development.

---

## 1. Project Layout

```
VPN1/
├── CMakeLists.txt
├── PRD.md
├── TDD.md
├── PLAN.md
├── README.md
├── src/
│   ├── main.cpp
│   ├── tun.h / tun.cpp
│   ├── crypto.h / crypto.cpp
│   ├── udp.h / udp.cpp
│   └── loop.h / loop.cpp
├── tests/
│   └── test_crypto.cpp
├── docs/
│   └── how-it-works.md
├── setup_server.sh
├── setup_client.sh
└── teardown_client.sh
```

---

## 2. Constants & Defaults

Defined in a shared header (e.g. `src/common.h`):

| Constant | Value | Notes |
|---|---|---|
| `DEFAULT_PORT` | `51820` | UDP port, both client and server |
| `DEFAULT_KEY_PATH` | `"./shared.key"` | 32-byte raw key file |
| `DEFAULT_TUN_IP_SERVER` | `"10.0.0.1"` | `/24` |
| `DEFAULT_TUN_IP_CLIENT` | `"10.0.0.2"` | `/24` |
| `TUN_NETMASK` | `/24` (`255.255.255.0`) | |
| `TUN_MTU` | `1400` | |
| `TUN_DEVICE_NAME` | `"tun0"` | requested name; kernel may assign another |
| `BUF_SIZE` | `2048` | shared buffer size for TUN reads and UDP I/O |
| `NONCE_LEN` | `12` | bytes |
| `TAG_LEN` | `16` | bytes |
| `KEY_LEN` | `32` | bytes (AES-256) |
| `CRYPTO_OVERHEAD` | `NONCE_LEN + TAG_LEN = 28` | bytes |

---

## 3. Wire Format

Every UDP datagram exchanged between client and server has this layout:

```
+------------------+------------------+----------------------------+
|  nonce (12 B)    |  GCM tag (16 B)  |  ciphertext (N bytes)       |
+------------------+------------------+----------------------------+
```

- `N` = length of the plaintext IP packet read from the TUN device.
- Total datagram size = `28 + N`.
- A received datagram with length `< 28` is malformed and dropped (FR-2.2/2.3) with a `[warn]` log line.
- No version byte, packet-type field, or length prefix — the entire datagram other than the 28-byte header is ciphertext.

---

## 4. Crypto Module (`crypto.h` / `crypto.cpp`)

AES-256-GCM via OpenSSL EVP. Fresh `EVP_CIPHER_CTX` per call (no AAD).

```cpp
// crypto.h
#pragma once
#include <cstdint>
#include <cstddef>

constexpr size_t KEY_LEN = 32;
constexpr size_t NONCE_LEN = 12;
constexpr size_t TAG_LEN = 16;

// Encrypts `plaintext` (pt_len bytes) under `key`.
// Generates a random 12-byte nonce internally (via RAND_bytes) and writes it to out_nonce.
// Writes the 16-byte GCM tag to out_tag and the ciphertext (pt_len bytes) to out_ciphertext.
// Returns true on success.
bool aead_encrypt(const uint8_t key[KEY_LEN],
                   const uint8_t* plaintext, size_t pt_len,
                   uint8_t out_nonce[NONCE_LEN],
                   uint8_t out_tag[TAG_LEN],
                   uint8_t* out_ciphertext);

// Decrypts `ciphertext` (ct_len bytes) under `key`, `nonce`, and `tag`.
// Writes ct_len bytes of plaintext to out_plaintext.
// Returns false if GCM tag verification fails (out_plaintext is undefined in that case).
bool aead_decrypt(const uint8_t key[KEY_LEN],
                   const uint8_t nonce[NONCE_LEN],
                   const uint8_t tag[TAG_LEN],
                   const uint8_t* ciphertext, size_t ct_len,
                   uint8_t* out_plaintext);

// Loads exactly KEY_LEN bytes from `path` into `out_key`.
// Returns false (with a [error] message) if the file size != KEY_LEN or cannot be read.
// Prints a [warn] to stderr (but still returns true) if the file's permission bits
// grant access to group or other.
bool load_key(const char* path, uint8_t out_key[KEY_LEN]);
```

Implementation notes:
- `aead_encrypt`: `RAND_bytes(out_nonce, NONCE_LEN)` → `EVP_EncryptInit_ex` with `EVP_aes_256_gcm()` → `EVP_EncryptUpdate` → `EVP_EncryptFinal_ex` → `EVP_CIPHER_CTX_ctrl(EVP_CTRL_GCM_GET_TAG)`.
- `aead_decrypt`: `EVP_DecryptInit_ex` → `EVP_DecryptUpdate` → `EVP_CIPHER_CTX_ctrl(EVP_CTRL_GCM_SET_TAG)` → `EVP_DecryptFinal_ex` return value indicates tag validity.
- `load_key`: `stat()` for size check, `open`+`read` for contents, `st_mode & (S_IRWXG | S_IRWXO)` for the permission warning.

---

## 5. TUN Module (`tun.h` / `tun.cpp`)

```cpp
// tun.h
#pragma once
#include <string>
#include <cstdint>

// Opens /dev/net/tun, creates a TUN device via TUNSETIFF with
// IFF_TUN | IFF_NO_PI, requesting `requested_name` (e.g. "tun0").
// On success, `actual_name_out` is set to the kernel-assigned device name
// (may differ from requested_name if it was taken).
// Returns the TUN file descriptor, or -1 on error (prints [error]).
int tun_create(const char* requested_name, std::string& actual_name_out);

// Assigns `ip_addr/24`, sets MTU to `mtu`, and brings the interface up,
// using ioctl(SIOCSIFADDR, SIOCSIFNETMASK, SIOCSIFMTU, SIOCSIFFLAGS)
// on a throwaway AF_INET socket.
// Returns true on success.
bool tun_configure(const char* device_name, const char* ip_addr, int mtu);
```

Implementation notes:
- Every function touching `ioctl`/`open` has a comment explaining the call (NFR-2.2).
- `tun_create` opens `/dev/net/tun`, sets `ifr.ifr_flags = IFF_TUN | IFF_NO_PI`, copies `requested_name` into `ifr.ifr_name`, calls `ioctl(fd, TUNSETIFF, &ifr)`, then reads back `ifr.ifr_name` for `actual_name_out`.
- `tun_configure` uses a separate `socket(AF_INET, SOCK_DGRAM, 0)` purely as a handle for the `SIOCSIF*` ioctls (standard Linux idiom — these ioctls are not actually about sending/receiving on that socket).
- No explicit teardown (FR-1.4) — closing the fd is sufficient.

---

## 6. UDP Module (`udp.h` / `udp.cpp`)

```cpp
// udp.h
#pragma once
#include <netinet/in.h>
#include <cstdint>

// Server mode: creates a UDP socket and binds it to 0.0.0.0:`listen_port`.
// Returns the socket fd, or -1 on error.
int udp_server_socket(uint16_t listen_port);

// Client mode: creates a UDP socket and connect()s it to `server_ip:server_port`.
// connect() on a UDP socket sets the default destination for send()/recv()
// and causes the kernel to filter out packets from other sources.
// Returns the socket fd, or -1 on error.
int udp_client_socket(const char* server_ip, uint16_t server_port);
```

---

## 7. Core Loop (`loop.h` / `loop.cpp`)

```cpp
// loop.h
#pragma once
#include <cstdint>
#include <netinet/in.h>
#include "crypto.h"

// Runs the poll()-based event loop until SIGINT.
// tun_fd:    TUN device fd (IFF_NO_PI — raw IP packets)
// udp_fd:    UDP socket fd
//            - client mode: connect()-ed, peer_known starts true, peer_addr unused
//            - server mode: bound but not connected; peer learned dynamically
// key:       32-byte shared key
// is_server: true if running in server mode (enables dynamic peer learning)
void run_loop(int tun_fd, int udp_fd, const uint8_t key[KEY_LEN], bool is_server);
```

### Loop body (pseudocode)

```cpp
static volatile sig_atomic_t g_running = 1;
void on_sigint(int) { g_running = 0; }

void run_loop(int tun_fd, int udp_fd, const uint8_t key[KEY_LEN], bool is_server) {
    signal(SIGINT, on_sigint);

    struct sockaddr_in peer_addr{};
    bool peer_known = !is_server;  // client already has a fixed peer via connect()

    struct pollfd fds[2] = {
        { .fd = tun_fd, .events = POLLIN },
        { .fd = udp_fd, .events = POLLIN },
    };

    uint8_t tun_buf[BUF_SIZE];
    uint8_t udp_buf[BUF_SIZE];

    while (g_running) {
        int n = poll(fds, 2, -1);
        if (n < 0) {
            if (errno == EINTR) break;
            fprintf(stderr, "[error] poll failed: %s\n", strerror(errno));
            break;
        }

        // TUN -> encrypt -> UDP
        if (fds[0].revents & POLLIN) {
            ssize_t pt_len = read(tun_fd, tun_buf, sizeof(tun_buf));
            if (pt_len > 0) {
                uint8_t out[NONCE_LEN + TAG_LEN + BUF_SIZE];
                aead_encrypt(key, tun_buf, pt_len, out, out + NONCE_LEN, out + NONCE_LEN + TAG_LEN);
                size_t out_len = NONCE_LEN + TAG_LEN + pt_len;
                if (!is_server) {
                    send(udp_fd, out, out_len, 0);
                } else if (peer_known) {
                    sendto(udp_fd, out, out_len, 0, (sockaddr*)&peer_addr, sizeof(peer_addr));
                }
                // else: no known peer yet, drop (FR-2.4)
            }
        }

        // UDP -> decrypt -> TUN
        if (fds[1].revents & POLLIN) {
            struct sockaddr_in src{};
            socklen_t src_len = sizeof(src);
            ssize_t n2 = recvfrom(udp_fd, udp_buf, sizeof(udp_buf), 0, (sockaddr*)&src, &src_len);
            if (n2 >= (ssize_t)(NONCE_LEN + TAG_LEN)) {
                size_t ct_len = n2 - NONCE_LEN - TAG_LEN;
                uint8_t plaintext[BUF_SIZE];
                if (aead_decrypt(key, udp_buf, udp_buf + NONCE_LEN,
                                  udp_buf + NONCE_LEN + TAG_LEN, ct_len, plaintext)) {
                    write(tun_fd, plaintext, ct_len);
                    if (is_server) { peer_addr = src; peer_known = true; }
                } else {
                    fprintf(stderr, "[warn] dropped packet: GCM tag verification failed\n");
                }
            } else if (n2 >= 0) {
                fprintf(stderr, "[warn] dropped packet: too short (%zd bytes)\n", n2);
            }
        }
    }

    close(tun_fd);
    close(udp_fd);
}
```

Notes:
- Partial reads/writes on `tun_fd` are not expected in practice (TUN device delivers whole packets per `read()`); no partial-write retry loop is implemented for v1.
- `send()`/`recvfrom()` on a non-blocking-free poll-driven socket are expected to complete fully for datagram sizes ≤ `BUF_SIZE`.

---

## 8. `main.cpp` — CLI & Orchestration

### Argument parsing (`getopt_long`)

| Flag | Arg | Default | Notes |
|---|---|---|---|
| `--mode` | `client`\|`server` | *required* | |
| `--server` | IP | *required in client mode* | server's address to connect to |
| `--port` | uint16 | `51820` | server: listen port; client: server's port |
| `--tun-ip` | IP | `10.0.0.1` (server) / `10.0.0.2` (client) | |
| `--key` | path | `./shared.key` | |

### Startup sequence

1. Parse args, apply mode-dependent defaults.
2. `load_key(key_path, key)` — exit(1) on failure.
3. `tun_create(TUN_DEVICE_NAME, actual_name)` — exit(1) on failure.
4. `tun_configure(actual_name, tun_ip, TUN_MTU)` — exit(1) on failure.
5. Server: `udp_server_socket(port)`. Client: `udp_client_socket(server_ip, port)` — exit(1) on failure.
6. `run_loop(tun_fd, udp_fd, key, is_server)` — blocks until SIGINT.
7. Process exits 0.

---

## 9. Build System (`CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.10)
project(tunnelink CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

add_executable(tunnelink
    src/main.cpp
    src/tun.cpp
    src/crypto.cpp
    src/udp.cpp
    src/loop.cpp
)
target_link_libraries(tunnelink PRIVATE OpenSSL::Crypto)

enable_testing()
add_executable(test_crypto tests/test_crypto.cpp src/crypto.cpp)
target_link_libraries(test_crypto PRIVATE OpenSSL::Crypto)
add_test(NAME crypto_roundtrip COMMAND test_crypto)
```

Build: `cmake -B build && cmake --build build`. Test: `ctest --test-dir build` or run `./build/test_crypto` directly.

---

## 10. Tests (`tests/test_crypto.cpp`)

Plain `assert()`-based, no framework:

1. `test_roundtrip()` — encrypt a sample plaintext, decrypt it, assert the result equals the original.
2. `test_tamper_detection()` — encrypt, flip one byte of the ciphertext (or tag), assert `aead_decrypt` returns `false`.

`main()` runs both and returns 0 on success (non-zero / aborted assert on failure), enabling `ctest` integration.

---

## 11. Scripts

### `setup_server.sh <egress_iface>`

```bash
#!/bin/bash
set -e
IFACE=$1
sysctl -w net.ipv4.ip_forward=1
iptables -t nat -A POSTROUTING -o "$IFACE" -j MASQUERADE
iptables -A FORWARD -i tun0 -o "$IFACE" -j ACCEPT
iptables -A FORWARD -i "$IFACE" -o tun0 -m state --state ESTABLISHED,RELATED -j ACCEPT
```

### `setup_client.sh <server_ip>`

```bash
#!/bin/bash
set -e
SERVER_IP=$1
ORIG_GW=$(ip route show default | awk '{print $3}')
ORIG_IFACE=$(ip route show default | awk '{print $5}')
echo "$ORIG_GW $ORIG_IFACE" > /tmp/tunnelink_orig_gw

ip route add "$SERVER_IP" via "$ORIG_GW" dev "$ORIG_IFACE"
ip route del default
ip route add default dev tun0
```

### `teardown_client.sh <server_ip>`

```bash
#!/bin/bash
set -e
SERVER_IP=$1
read ORIG_GW ORIG_IFACE < /tmp/tunnelink_orig_gw

ip route del default
ip route del "$SERVER_IP" via "$ORIG_GW" dev "$ORIG_IFACE"
ip route add default via "$ORIG_GW" dev "$ORIG_IFACE"
```

---

## 12. Logging Convention

- All output goes to `stderr`.
- Startup/fatal errors: `fprintf(stderr, "[error] ...\n"); exit(1);`
- Runtime warnings (dropped packets): `fprintf(stderr, "[warn] ...\n");` — loop continues.
- No rate limiting in v1 (documented limitation).
