#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <getopt.h>
#include <unistd.h>

#include "common.h"
#include "crypto.h"
#include "loop.h"
#include "tun.h"
#include "udp.h"

static bool parse_port(const char *value, uint16_t &port_out)
{
    if (!value || *value == '\0')
        return false;

    errno = 0;
    char *end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno != 0 || *end != '\0' || parsed == 0 || parsed > 65535)
        return false;

    port_out = static_cast<uint16_t>(parsed);
    return true;
}

static void usage(const char *prog)
{
    std::fprintf(stderr,
                 "Usage: %s --mode client|server [options]\n"
                 "  --mode <client|server>   (required)\n"
                 "  --server <ip>            server address (required in client mode)\n"
                 "  --port <port>            UDP port (default %u)\n"
                 "  --tun-ip <ip>             TUN interface IP (default %s server / %s client)\n"
                 "  --key <path>              pre-shared key file (default %s)\n",
                 prog, DEFAULT_PORT, DEFAULT_TUN_IP_SERVER, DEFAULT_TUN_IP_CLIENT, DEFAULT_KEY_PATH);
}

int main(int argc, char **argv)
{
    std::string mode;
    std::string server_ip;
    std::string tun_ip;
    std::string key_path = DEFAULT_KEY_PATH;
    uint16_t port = DEFAULT_PORT;

    enum
    {
        OPT_MODE = 1,
        OPT_SERVER,
        OPT_PORT,
        OPT_TUN_IP,
        OPT_KEY
    };
    static struct option long_opts[] = {
        {"mode", required_argument, nullptr, OPT_MODE},
        {"server", required_argument, nullptr, OPT_SERVER},
        {"port", required_argument, nullptr, OPT_PORT},
        {"tun-ip", required_argument, nullptr, OPT_TUN_IP},
        {"key", required_argument, nullptr, OPT_KEY},
        {nullptr, 0, nullptr, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_opts, nullptr)) != -1)
    {
        switch (opt)
        {
        case OPT_MODE:
            mode = optarg;
            break;
        case OPT_SERVER:
            server_ip = optarg;
            break;
        case OPT_PORT:
            if (!parse_port(optarg, port))
            {
                std::fprintf(stderr,
                             "[error] invalid port '%s': expected an integer from 1 to 65535\n",
                             optarg);
                return 1;
            }
            break;
        case OPT_TUN_IP:
            tun_ip = optarg;
            break;
        case OPT_KEY:
            key_path = optarg;
            break;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    bool is_server;
    if (mode == "server")
    {
        is_server = true;
    }
    else if (mode == "client")
    {
        is_server = false;
    }
    else
    {
        std::fprintf(stderr, "[error] --mode must be 'client' or 'server'\n");
        usage(argv[0]);
        return 1;
    }

    if (!is_server && server_ip.empty())
    {
        std::fprintf(stderr, "[error] --server <ip> is required in client mode\n");
        usage(argv[0]);
        return 1;
    }

    if (tun_ip.empty())
    {
        tun_ip = is_server ? DEFAULT_TUN_IP_SERVER : DEFAULT_TUN_IP_CLIENT;
    }

    uint8_t key[KEY_LEN];
    if (!load_key(key_path.c_str(), key))
    {
        return 1;
    }

    std::string actual_name;
    int tun_fd = tun_create(TUN_DEVICE_NAME, actual_name);
    if (tun_fd < 0)
    {
        return 1;
    }

    if (!tun_configure(actual_name.c_str(), tun_ip.c_str(), TUN_MTU))
    {
        close(tun_fd);
        return 1;
    }

    int udp_fd;
    if (is_server)
    {
        udp_fd = udp_server_socket(port);
    }
    else
    {
        udp_fd = udp_client_socket(server_ip.c_str(), port);
    }
    if (udp_fd < 0)
    {
        close(tun_fd);
        return 1;
    }

    std::printf("TunneLink running as %s: TUN '%s' (%s/24, MTU %d), UDP port %u. Press Ctrl+C to exit.\n",
                mode.c_str(), actual_name.c_str(), tun_ip.c_str(), TUN_MTU, port);

    run_loop(tun_fd, udp_fd, key, is_server);

    std::printf("\nExiting.\n");
    return 0;
}
