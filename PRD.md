# TunneLink — Product Requirements Document

**Status:** v1 scope locked (minimal core)
**Platform:** Linux only
**License:** MIT (proposed)

> See `TDD.md` for the concrete technical design (wire format, module layout, interfaces) and `PLAN.md` for the phased development plan.

---

## 1. Overview

TunneLink is a point-to-point VPN tool written in C++. A single client connects to a single server over UDP. Traffic between them is encrypted with a shared secret key, and the server forwards the client's traffic to the internet and routes responses back.

The goal of v1 is to build the smallest possible thing that is honestly a working VPN — simple enough to fully understand end-to-end, while still touching the core skills relevant to systems/software engineering roles: C/C++, networking, OS-level programming (TUN interfaces), and cryptography.

### 1.1 Goals

- Build a working tunnel between exactly one client and one server
- Encrypt all traffic passing through the tunnel
- Route the client's internet traffic through the server and back
- Keep the implementation small enough that every line is understood, not copied
- Be buildable and runnable by a third party within 30 minutes of cloning

### 1.2 Non-Goals (v1)

- Any key exchange protocol (no Diffie-Hellman, no X25519, no handshake)
- Multithreading (single-threaded with simple I/O multiplexing is fine)
- Multi-client server support
- Cross-platform support (macOS, Windows)
- Config file parsing (hardcoded constants or simple CLI args only)
- Structured logging, stats, or observability beyond basic print statements
- Production-grade security guarantees

All of the above are listed in the **Future Improvements** section (Section 5) as a roadmap — they are deliberately deferred, not rejected.

---

## 2. Definitions

| Term | Meaning |
|---|---|
| TUN interface | Virtual network device that delivers raw IP packets to a userspace program instead of a physical network card |
| Shared key | A symmetric encryption key, generated once and copied to both client and server ahead of time (e.g., via `scp` or by hand) |
| Full tunnel | All of the client's internet traffic is routed through the VPN |

---

## 3. Functional Requirements

### 3.1 TUN Interface

- **FR-1.1:** The program must create a TUN interface on startup using `/dev/net/tun` and the `TUNSETIFF` ioctl with flags `IFF_TUN | IFF_NO_PI` (no 4-byte packet-info prefix), requesting device name `tun0` (falling back to whatever name the kernel assigns if `tun0` is taken).
- **FR-1.2:** The program must assign an IP address to the TUN interface using a `/24` subnet — server defaults to `10.0.0.1/24`, client defaults to `10.0.0.2/24` (overridable via `--tun-ip`) — set the interface MTU to `1400`, and bring the interface up.
- **FR-1.3:** The program must be able to read raw IP packets from the TUN interface and write raw IP packets to it.
- **FR-1.4:** On shutdown (`Ctrl+C` / `SIGINT`), the program should exit cleanly — closing the TUN file descriptor is sufficient; explicit interface teardown is not required (the OS reclaims it).

### 3.2 Transport (UDP)

- **FR-2.1:** Client and server communicate exclusively over a single UDP socket.
- **FR-2.2:** Each UDP datagram has the layout `[nonce (12 bytes)][GCM tag (16 bytes)][ciphertext (variable length)]`. No additional packet-type header is used — every UDP datagram received is assumed to be an encrypted IP packet in this format. Datagrams shorter than 28 bytes (nonce + tag) are dropped as malformed.
- **FR-2.3:** If a received packet fails to decrypt (see FR-3.3) or is malformed (too short), it is dropped with a one-line warning to stderr — the program must not crash. No rate limiting is applied in v1 (documented as a known limitation).
- **FR-2.4:** The server learns the client's `(IP, port)` dynamically: on every UDP datagram that decrypts successfully, the server records its source address as the current peer and uses it as the destination for subsequent TUN→UDP traffic. Until a peer is known, TUN→UDP traffic is dropped. The client connects its UDP socket to the configured server address at startup.

### 3.3 Encryption

- **FR-3.1:** Both client and server load the same pre-shared symmetric key from a local file at startup (e.g., `./shared.key`, a raw 32-byte file). This file is generated once (e.g., via `openssl rand -out shared.key 32`) and copied to both machines manually before running the program.
- **FR-3.2:** Every packet sent over UDP is encrypted with AES-256-GCM using this shared key. Each packet includes a random 12-byte nonce (sent alongside the ciphertext) and the GCM authentication tag.
- **FR-3.3:** On receipt, the program decrypts using the shared key and the nonce included in the packet. If the GCM tag fails verification, the packet is dropped.
- **FR-3.4:** All encryption/decryption uses OpenSSL's EVP interface — no hand-written crypto primitives.

### 3.4 Core Loop (Single-Threaded I/O Multiplexing)

- **FR-4.1:** Each side (client and server) runs a single loop that waits on both the TUN file descriptor and the UDP socket simultaneously, using `poll()`. The loop implementation is shared between client and server modes — only setup (TUN config, socket connect vs. bind) differs.
- **FR-4.2:** When a packet arrives on the TUN interface, it is encrypted and sent over the UDP socket.
- **FR-4.3:** When a packet arrives on the UDP socket, it is decrypted and written to the TUN interface.
- **FR-4.4:** No threads are required for v1 — `select()`/`poll()` on two file descriptors is sufficient and simpler to reason about.

### 3.5 Server-Side Forwarding (NAT)

- **FR-5.1:** The server enables IP forwarding (`net.ipv4.ip_forward=1`).
- **FR-5.2:** The server applies a `POSTROUTING` `MASQUERADE` rule on its egress interface so that packets arriving from the client (via the server's TUN interface) appear to originate from the server when sent to the internet, and responses are routed back correctly. Explicit `FORWARD` `ACCEPT` rules between `tun0` and the egress interface (both directions) are also added, in case the default `FORWARD` policy is `DROP`.
- **FR-5.3:** These `iptables`/`sysctl` commands are not issued by the program itself. They are provided as `setup_server.sh <egress_iface>`, a script run once (after the server program has started and `tun0` exists) that applies FR-5.1 and FR-5.2.

### 3.6 Client-Side Routing

- **FR-6.1:** The project provides a short setup script (`setup_client.sh <server_ip>`) that:
  - Auto-detects the current default gateway and interface via `ip route show default`, and saves them (e.g. to `/tmp/tunnelink_orig_gw`) for teardown
  - Adds a host route to `<server_ip>` via that original gateway (so the connection to the server itself doesn't get routed through the tunnel)
  - Replaces the default route to point at the `tun0` interface
- **FR-6.2:** A matching `teardown_client.sh <server_ip>` reads the saved gateway/interface and reverses these changes (restores the original default route, removes the host route).
- **FR-6.3:** The TunneLink binary does not modify the routing table itself.

### 3.7 Configuration

- **FR-7.1:** The program accepts configuration via `getopt_long`-style long options: `--mode client|server`, `--server <ip>` (required in client mode — the server's address to connect to), `--port <port>`, `--tun-ip <ip>`, `--key <path>`.
- **FR-7.2:** Defaults are hardcoded for anything not provided: `--port` defaults to `51820`; `--tun-ip` defaults to `10.0.0.1` for server mode and `10.0.0.2` for client mode; `--key` defaults to `./shared.key`.

### 3.8 Build & Testing

- **FR-8.1:** The project builds via a single `cmake -B build && cmake --build build` command (or a simple `Makefile`, whichever is faster to get working).
- **FR-8.2:** A minimal manual test procedure is documented in the README: run server and client (e.g., in two network namespaces on the same VM, or two VMs), confirm `ping 8.8.8.8` works from the client through the tunnel.
- **FR-8.3:** A small assert-based test (`tests/test_crypto.cpp`, built as a separate CMake/ctest target) covers the encrypt/decrypt round trip: encrypt then decrypt confirms output matches input, and tampering with the ciphertext or tag confirms decryption fails.

---

## 4. Non-Functional Requirements

### 4.1 Security

- **NFR-1.1:** The README must include a short, honest "Limitations" section: the shared key must be distributed securely out-of-band (e.g., `scp`, not email); if the key is compromised, all traffic (past and future, while reused) is compromised; this project has not been audited and should not be used to protect sensitive traffic.
- **NFR-1.2:** All cryptographic operations use OpenSSL's EVP interface.

### 4.2 Code Quality

- **NFR-2.1:** Code should be organized into a small number of clearly-named files (e.g., `tun.cpp`, `crypto.cpp`, `main.cpp`) rather than one large file — but elaborate class hierarchies or abstraction layers are not required for v1.
- **NFR-2.2:** Every function involving raw syscalls (TUN setup, socket setup) should have a comment explaining what it does and why, since this is also a learning project.

### 4.3 Documentation

- **NFR-3.1:** `README.md` includes: what the project is, the Limitations section (4.1), build instructions, and a step-by-step "how to run it" walkthrough.
- **NFR-3.2:** A short `docs/how-it-works.md` (or a diagram) showing the packet flow: app → TUN → encrypt → UDP → (network) → UDP → decrypt → TUN → app. This is the single most useful artifact for explaining the project in an interview.

### 4.4 Platform

- **NFR-4.1:** v1 targets Ubuntu 22.04+ (or equivalent recent Linux). Requires root or `CAP_NET_ADMIN` to create the TUN interface and configure `iptables` — documented in the README.

---

## 5. Future Improvements (Explicitly Out of Scope for v1)

These are deferred, not rejected — each represents a natural "v2" improvement with its own story ("I built a working VPN, then upgraded its security model by adding X25519 key exchange"). Listed roughly in order of suggested priority:

1. **Ephemeral key exchange (X25519 + HKDF):** Replace the static shared key with a per-session key derived via X25519 ECDH, providing forward secrecy. The shared key becomes an authentication secret (HMAC over the handshake) rather than the encryption key itself.
2. **Multithreading:** Split the single `select()`/`poll()` loop into separate threads for TUN→socket and socket→TUN directions, using `std::thread` and `std::mutex`/`std::condition_variable` for any shared state.
3. **Config file (TOML):** Replace CLI-args-only configuration with a TOML config file, keeping CLI flags as overrides.
4. **Wire protocol / packet types:** Introduce a small header distinguishing packet types (handshake messages, data, keepalive) to support #1 and #5.
5. **Keepalive / dead-peer detection:** Periodic keepalive packets and a timeout to detect a dropped connection.
6. **Multi-client support:** Per-client session state, an IP address pool, and return-traffic routing by session.
7. **Logging & stats:** Structured log levels and a `SIGUSR1`-triggered stats dump (bytes sent/received, packets dropped).

---

## 6. Resolved Design Decisions (formerly Open Questions / Risks)

- **MTU:** TUN MTU is set to `1400` (FR-1.2). With 28 bytes of crypto overhead (12-byte nonce + 16-byte GCM tag) plus UDP/IP headers, the resulting UDP datagram stays under the typical 1500-byte Ethernet MTU, avoiding fragmentation of the tunnel's own traffic.
- **Key file permissions:** On startup, the program checks the key file's permission bits and prints a warning (but does not fail) to stderr if group/other have any access — see FR-3.1 and `TDD.md`.
- **iptables vs nftables:** v1 documents and scripts `iptables` commands (`setup_server.sh`); systems that are `nftables`-only may need translated commands — noted as a known limitation in the README.

---

## 7. Abbreviations

| Abbreviation | Meaning |
|---|---|
| VPN | Virtual Private Network |
| TUN | Network TUNnel (virtual network device / `/dev/net/tun`) |
| UDP | User Datagram Protocol |
| IP | Internet Protocol |
| AES | Advanced Encryption Standard |
| GCM | Galois/Counter Mode (AEAD cipher mode) |
| AEAD | Authenticated Encryption with Associated Data |
| AAD | Additional Authenticated Data |
| HKDF | HMAC-based Key Derivation Function |
| HMAC | Hash-based Message Authentication Code |
| X25519 | Curve25519-based ECDH key exchange |
| ECDH | Elliptic-Curve Diffie–Hellman |
| NAT | Network Address Translation |
| MTU | Maximum Transmission Unit |
| CLI | Command-Line Interface |
| FR | Functional Requirement |
| NFR | Non-Functional Requirement |
| PRD | Product Requirements Document |
| TDD | Technical Design Document |
| EVP | OpenSSL's "envelope" high-level crypto API |
| CSPRNG | Cryptographically Secure Pseudo-Random Number Generator |
| PtP | Point-to-Point |
| PI | Packet Information (TUN header prefix, as in `IFF_NO_PI`) |
| fd | File Descriptor |
| ioctl | Input/Output Control (syscall) |
| SIGINT | Signal Interrupt (Ctrl+C) |
| EINTR | Error: Interrupted (errno value) |
| EAGAIN | Error: Try Again (errno value) |
| TCP | Transmission Control Protocol |
| ICMP | Internet Control Message Protocol |
| VM | Virtual Machine |