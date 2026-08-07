# TunneLink — Development Plan

Phased build order. Each phase has a goal, deliverables, and an exit check before moving on. Phases are ordered to de-risk the hardest/most foundational pieces first and to keep each phase independently testable.

---

## Phase 0 — Project Scaffolding

**Goal:** Buildable empty project.

- Create directory layout per `TDD.md` §1 (`src/`, `tests/`, `docs/`).
- `CMakeLists.txt` with `find_package(OpenSSL REQUIRED)`, empty `tunnelink` and `test_crypto` targets, `enable_testing()`.
- `src/common.h` with all constants from `TDD.md` §2.
- Stub `main.cpp` that just prints a message and exits 0.

**Exit check:** `cmake -B build && cmake --build build` succeeds and `./build/tunnelink` runs.

---

## Phase 1 — Crypto Module + Test

**Goal:** AES-256-GCM encrypt/decrypt and key loading, fully tested in isolation (no TUN/UDP dependencies).

- Implement `crypto.h`/`crypto.cpp` per `TDD.md` §4: `aead_encrypt`, `aead_decrypt`, `load_key`.
- Implement `tests/test_crypto.cpp`: round-trip test + tamper-detection test (FR-8.3).
- Generate a throwaway `shared.key` via `openssl rand -out shared.key 32` for manual testing of `load_key` (including the permission-warning path via `chmod`).

**Exit check:** `ctest --test-dir build` passes both crypto tests. Manually verify `load_key` warns on a `chmod 644 shared.key` file and fails cleanly on a wrong-size file.

---

## Phase 2 — TUN Module

**Goal:** Create and configure a TUN interface, verifiable with standard Linux tools.

- Implement `tun.h`/`tun.cpp` per `TDD.md` §5: `tun_create` (IFF_TUN | IFF_NO_PI, name `tun0`), `tun_configure` (IP/24, MTU 1400, up).
- Wire into `main.cpp` behind a temporary `--mode` flag (or hardcode for this phase) so a quick test binary can create+configure `tun0` and then just block (e.g. `pause()`/sleep loop) so the interface stays up for inspection.
- Comment every syscall/ioctl per NFR-2.2.

**Exit check:** Run as root; `ip addr show tun0` shows the expected `10.0.0.x/24`, MTU `1400`, state `UP`. `Ctrl+C` exits cleanly (fd close is sufficient, FR-1.4).

---

## Phase 3 — UDP Module

**Goal:** Socket setup for both modes.

- Implement `udp.h`/`udp.cpp` per `TDD.md` §6: `udp_server_socket`, `udp_client_socket`.
- Quick manual test: server binds and a client connects from the same host (loopback `127.0.0.1`); confirm with `ss -u -a` that the socket is in the expected state.

**Exit check:** Both socket constructors return valid fds; basic `send`/`recv` of a raw test buffer works over loopback.

---

## Phase 4 — Core Loop & CLI Integration

**Goal:** Full single-machine pipeline: TUN ↔ encrypt/decrypt ↔ UDP, both directions, both modes.

- Implement `loop.h`/`loop.cpp` per `TDD.md` §7 (poll-based loop, SIGINT handling, server peer-learning per FR-2.4).
- Implement full `getopt_long` CLI parsing in `main.cpp` per `TDD.md` §8 (defaults, mode-dependent `--tun-ip`).
- Wire startup sequence: load key → create/configure TUN → create socket → `run_loop`.

**Exit check (local, same machine, two network namespaces or two terminals with separate TUN devices):**
- Run server (`--mode server`) and client (`--mode client --server 127.0.0.1`).
- `ping -I tun0 10.0.0.1` from the client side (or equivalent) produces ICMP traffic that traverses TUN → encrypt → UDP loopback → decrypt → TUN on the server side.
- Tampering with a packet (or killing one side) demonstrates clean drop/log behavior (FR-2.3) and clean `Ctrl+C` shutdown (FR-1.4) without crashes.

This is the first point where the "is it honestly a working VPN" claim can start to be checked end-to-end, even before NAT/routing.

---

## Phase 5 — Server NAT Setup Script

**Goal:** Server can forward client traffic to the internet and route responses back.

- Write `setup_server.sh <egress_iface>` per `TDD.md` §11 (FR-5.1/5.2/5.3).
- Test on a real or VM server with internet access: start `tunnelink --mode server`, run `setup_server.sh eth0`, confirm `iptables -t nat -L` and `sysctl net.ipv4.ip_forward` reflect the changes.

**Exit check:** From the server's `tun0` peer (manually add a temporary route on a second test machine or namespace pointing at the server's tun0 IP), `ping 8.8.8.8` succeeds — confirms MASQUERADE + FORWARD rules work before involving the client's routing changes.

---

## Phase 6 — Client Routing Scripts

**Goal:** Client's full-tunnel routing (FR-6.1/6.2).

- Write `setup_client.sh <server_ip>` and `teardown_client.sh <server_ip>` per `TDD.md` §11.
- Test on the client: run `tunnelink --mode client --server <server_ip>`, then `setup_client.sh <server_ip>`.
- Confirm `ip route` shows the host route to `<server_ip>` via the original gateway, and the default route now points at `tun0`.
- Run `teardown_client.sh <server_ip>` and confirm `ip route` is restored to its original state.

**Exit check:** With the script applied and the tunnel running, normal internet access (e.g. `curl ifconfig.me`) still works and reports the **server's** public IP.

---

## Phase 7 — End-to-End Test (Two Hosts/VMs)

**Goal:** Full FR-8.2 manual test procedure, documented in the README.

- Two VMs (or two network namespaces on one VM): one runs `--mode server` + `setup_server.sh`, the other runs `--mode client --server <server_ip>` + `setup_client.sh`.
- `ping 8.8.8.8` from the client succeeds through the tunnel.
- Document exact commands and expected output in `README.md`.

**Exit check:** A third party following the README's walkthrough gets a working tunnel within 30 minutes (PRD goal).

---

## Phase 8 — Documentation

**Goal:** NFR-3.1/3.2 satisfied.

- `README.md`: project description, Limitations section (NFR-1.1: shared key distribution, key compromise implications, no audit), build instructions, step-by-step run walkthrough (from Phase 7).
- `docs/how-it-works.md`: packet flow diagram (app → TUN → encrypt → UDP → network → UDP → decrypt → TUN → app), referencing the wire format from `TDD.md` §3.

**Exit check:** README and how-it-works.md reviewed for accuracy against the final implementation; both PRD goals ("buildable/runnable within 30 minutes" and "every line understood") are addressed by the docs.

---

## Notes on Ordering

- Phases 1–3 are independent of each other and could be parallelized if multiple people were working on this; here they're sequenced for a single developer to build understanding incrementally.
- Phase 4 is the critical integration milestone — everything before it is a building block, everything after it is "making the VPN actually useful" (NAT, routing, docs).
- If Phase 4's loopback test reveals issues with the wire format, buffer sizing, or peer-learning logic (`TDD.md` §3/§7), resolve them there before moving to Phases 5–7, since those phases assume the core loop is correct.
