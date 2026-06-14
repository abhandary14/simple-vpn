#include <csignal>
#include <cstdio>
#include <string>

#include <unistd.h>

#include "common.h"
#include "tun.h"

// Phase 2 scaffolding: create and configure the TUN interface and hold it
// open until SIGINT, so it can be inspected with `ip addr show tun0`.
// CLI argument parsing and the encrypt/UDP/loop pieces arrive in later phases.

static volatile sig_atomic_t g_running = 1;

static void on_sigint(int) {
    g_running = 0;
}

int main(int argc, char** argv) {
    const char* ip = (argc > 1) ? argv[1] : DEFAULT_TUN_IP_SERVER;

    std::string actual_name;
    int tun_fd = tun_create(TUN_DEVICE_NAME, actual_name);
    if (tun_fd < 0) {
        return 1;
    }

    if (!tun_configure(actual_name.c_str(), ip, TUN_MTU)) {
        close(tun_fd);
        return 1;
    }

    std::printf("TUN interface '%s' up with IP %s/24, MTU %d. Press Ctrl+C to exit.\n",
                 actual_name.c_str(), ip, TUN_MTU);

    signal(SIGINT, on_sigint);
    while (g_running) {
        pause();
    }

    close(tun_fd);
    std::printf("\nExiting.\n");
    return 0;
}