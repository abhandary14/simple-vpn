#pragma once

#include <string>

// Opens /dev/net/tun and creates a TUN device via TUNSETIFF with
// IFF_TUN | IFF_NO_PI (raw IP packets, no 4-byte protocol-info prefix),
// requesting `requested_name` (e.g. "tun0").
// On success, `actual_name_out` is set to the kernel-assigned device name
// (may differ from requested_name if it was already taken).
// Returns the TUN file descriptor, or -1 on error (prints [error]).
int tun_create(const char* requested_name, std::string& actual_name_out);

// Assigns `ip_addr` with the project's /24 netmask (TUN_NETMASK), sets the
// interface MTU to `mtu`, and brings the interface up.
// Returns true on success, false on error (prints [error]).
bool tun_configure(const char* device_name, const char* ip_addr, int mtu);