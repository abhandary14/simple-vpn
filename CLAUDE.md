# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TunneLink is a minimal point-to-point VPN written in C++ (Linux only). A single client connects to a single server over UDP; traffic is encrypted with AES-256-GCM using a pre-shared key, and the server NATs the client's traffic to the internet. The goal is a small, fully-understood implementation — avoid adding abstractions, threading, multi-client support, or config-file parsing; these are explicitly deferred (see PRD.md section 5).

**Read these three docs before making design decisions — they are the source of truth and supersede ad-hoc choices:**
- `PRD.md` — product requirements (functional/non-functional requirements, locked scope, abbreviations glossary)
- `TDD.md` — technical design (wire format, module interfaces, constants, build setup, shell scripts) — **the concrete spec for how things must be implemented**
- `PLAN.md` — phased development plan with exit criteria for each phase

If a design question comes up that isn't answered by these docs, resolve it explicitly (ask or document the decision) rather than guessing — these docs were produced via a deliberate decision-by-decision design review and should stay in sync with the code.

## Build & Test

```bash
cmake -B build && cmake --build build   # build everything
./build/tunnelink                        # run the main binary
./build/test_crypto                      # run crypto tests directly
ctest --test-dir build --output-on-failure   # run via ctest
```

There is no separate lint step. Re-run `cmake --build build` after any source change.

## Architecture

Source lives in `src/`, with one file per concern (per NFR-2.1): `main.cpp` (CLI + orchestration), `crypto.cpp`/`.h` (AEAD), and (per TDD.md, not yet implemented) `tun.cpp`/`.h`, `udp.cpp`/`.h`, `loop.cpp`/`.h`. Shared constants (ports, MTU, buffer sizes, crypto field lengths) live in `src/common.h` — pull from there rather than hardcoding magic numbers.

### Wire format (TDD.md section 3)
Every UDP datagram is `[nonce (12B)][GCM tag (16B)][ciphertext (N bytes)]` — 28 bytes of overhead, no version/type header. Datagrams under 28 bytes are dropped as malformed.

### Crypto module (`src/crypto.h`/`crypto.cpp`)
- `aead_encrypt`/`aead_decrypt`: AES-256-GCM via OpenSSL EVP, fresh `EVP_CIPHER_CTX` per call, no AAD. Nonces are random (`RAND_bytes`) per packet.
- `load_key`: reads exactly `KEY_LEN` (32) bytes from the key file; hard-fails on wrong size, warns (doesn't fail) if the file is group/other-readable.
- Tests (`tests/test_crypto.cpp`) cover round-trip and tamper detection (bit-flip in ciphertext or tag must fail to decrypt).

### Core loop (TDD.md section 7, not yet implemented)
A single `poll()`-based loop shared between client and server modes, watching the TUN fd and UDP socket. Server dynamically learns its peer's `(IP, port)` from the source address of the first successfully-decrypted packet (and updates on change); client `connect()`s its UDP socket at startup. SIGINT sets a `volatile sig_atomic_t` flag checked after `poll()` returns (handling EINTR) for clean shutdown — closing fds is sufficient, no explicit teardown.

### TUN setup (TDD.md section 5, not yet implemented)
`IFF_TUN | IFF_NO_PI` (no 4-byte packet-info prefix — raw IP packets only), device name `tun0` (fall back to kernel-assigned name if taken), `/24` addressing (server `10.0.0.1`, client `10.0.0.2`), MTU `1400`.

### Shell scripts (TDD.md section 11, not yet implemented)
`setup_server.sh <egress_iface>` (sysctl + iptables MASQUERADE/FORWARD), `setup_client.sh <server_ip>` / `teardown_client.sh <server_ip>` (route management, auto-detects and restores the original default gateway via `/tmp/tunnelink_orig_gw`). The binary itself never touches the routing table.

## Logging Convention

All output to stderr. Fatal/startup errors: `fprintf(stderr, "[error] ...\n"); exit(1);`. Runtime drops (bad decrypt, malformed packet): `fprintf(stderr, "[warn] ...\n");` and continue — no rate limiting (documented limitation).

## Development Process

Work proceeds phase-by-phase per `PLAN.md` (currently: Phase 0 scaffolding and Phase 1 crypto are done). Each phase has an explicit exit check — implement against that phase's scope only, don't pull forward work from later phases (e.g. don't add TUN/UDP/loop code while doing crypto work).