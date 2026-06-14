#include "tun.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/if_tun.h>

#include "common.h"

int tun_create(const char *requested_name, std::string &actual_name_out)
{
    // /dev/net/tun is the cloning device: opening it gives us a handle that
    // TUNSETIFF then binds to a specific (newly created) TUN interface.
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0)
    {
        std::fprintf(stderr, "[error] cannot open /dev/net/tun: %m\n");
        return -1;
    }

    struct ifreq ifr{};
    // IFF_TUN: deliver/accept raw IP packets (vs IFF_TAP for Ethernet frames).
    // IFF_NO_PI: don't prefix packets with the 4-byte protocol-info header,
    // so reads/writes are exactly the IP packets our wire format expects.
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (requested_name && *requested_name)
    {
        std::strncpy(ifr.ifr_name, requested_name, IFNAMSIZ - 1);
    }

    // TUNSETIFF creates the interface (or attaches to an existing
    // persistent one) and, on success, writes the actual interface name
    // back into ifr.ifr_name.
    if (ioctl(fd, TUNSETIFF, &ifr) < 0)
    {
        // If the requested name is already taken (e.g. tun0 in use by
        // another instance), fall back to a kernel-assigned name by
        // retrying with an empty ifr_name.
        if (errno == EBUSY && requested_name && *requested_name)
        {
            struct ifreq retry_ifr{};
            retry_ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
            if (ioctl(fd, TUNSETIFF, &retry_ifr) < 0)
            {
                std::fprintf(stderr, "[error] TUNSETIFF failed: %m\n");
                close(fd);
                return -1;
            }
            ifr = retry_ifr;
        }
        else
        {
            std::fprintf(stderr, "[error] TUNSETIFF failed: %m\n");
            close(fd);
            return -1;
        }
    }

    actual_name_out = ifr.ifr_name;
    return fd;
}

bool tun_configure(const char *device_name, const char *ip_addr, int mtu)
{
    // SIOCSIF* ioctls operate on network interfaces in general and need a
    // socket fd as a handle, but no data is ever sent/received on it.
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        std::fprintf(stderr, "[error] socket() failed: %m\n");
        return false;
    }

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, device_name, IFNAMSIZ - 1);

    // SIOCSIFADDR: assign the interface's IPv4 address.
    auto *addr = reinterpret_cast<struct sockaddr_in *>(&ifr.ifr_addr);
    addr->sin_family = AF_INET;
    if (inet_pton(AF_INET, ip_addr, &addr->sin_addr) != 1)
    {
        std::fprintf(stderr, "[error] invalid IP address '%s'\n", ip_addr);
        close(sock);
        return false;
    }
    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0)
    {
        std::fprintf(stderr, "[error] SIOCSIFADDR failed: %m\n");
        close(sock);
        return false;
    }

    // SIOCSIFNETMASK: set the /24 netmask shared by client and server.
    auto *mask = reinterpret_cast<struct sockaddr_in *>(&ifr.ifr_addr);
    mask->sin_family = AF_INET;
    inet_pton(AF_INET, TUN_NETMASK, &mask->sin_addr);
    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0)
    {
        std::fprintf(stderr, "[error] SIOCSIFNETMASK failed: %m\n");
        close(sock);
        return false;
    }

    // SIOCSIFMTU: shrink the MTU to leave room for the 28-byte crypto
    // overhead without fragmenting the outer UDP packet (TDD.md section 2).
    ifr.ifr_mtu = mtu;
    if (ioctl(sock, SIOCSIFMTU, &ifr) < 0)
    {
        std::fprintf(stderr, "[error] SIOCSIFMTU failed: %m\n");
        close(sock);
        return false;
    }

    // Bring the interface up: read current flags, OR in IFF_UP/IFF_RUNNING,
    // write them back via SIOCSIFFLAGS.
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
    {
        std::fprintf(stderr, "[error] SIOCGIFFLAGS failed: %m\n");
        close(sock);
        return false;
    }
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)
    {
        std::fprintf(stderr, "[error] SIOCSIFFLAGS failed: %m\n");
        close(sock);
        return false;
    }

    close(sock);
    return true;
}