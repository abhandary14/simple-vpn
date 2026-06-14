#pragma once

#include <cstdint>

// Server mode: creates a UDP socket and binds it to 0.0.0.0:`listen_port`.
// Returns the socket fd, or -1 on error (prints [error]).
int udp_server_socket(uint16_t listen_port);

// Client mode: creates a UDP socket and connect()s it to `server_ip:server_port`.
// connect() on a UDP socket sets the default destination for send()/recv()
// and causes the kernel to filter out packets from other sources.
// Returns the socket fd, or -1 on error (prints [error]).
int udp_client_socket(const char *server_ip, uint16_t server_port);