#pragma once

#include <cstdint>

#include "common.h"

// Runs the poll()-based event loop until SIGINT.
// tun_fd:    TUN device fd (IFF_NO_PI -- raw IP packets)
// udp_fd:    UDP socket fd
//            - client mode: connect()-ed, peer_known starts true, peer_addr unused
//            - server mode: bound but not connected; peer learned dynamically
// key:       32-byte shared key
// is_server: true if running in server mode (enables dynamic peer learning)
// Closes tun_fd and udp_fd before returning.
void run_loop(int tun_fd, int udp_fd, const uint8_t key[KEY_LEN], bool is_server);