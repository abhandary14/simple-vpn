#include "udp.h"

#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int udp_server_socket(uint16_t listen_port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        std::fprintf(stderr, "[error] socket() failed: %m\n");
        return -1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(listen_port);

    if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        std::fprintf(stderr, "[error] bind() failed: %m\n");
        close(fd);
        return -1;
    }

    return fd;
}

int udp_client_socket(const char *server_ip, uint16_t server_port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        std::fprintf(stderr, "[error] socket() failed: %m\n");
        return -1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) != 1)
    {
        std::fprintf(stderr, "[error] invalid server IP '%s'\n", server_ip);
        close(fd);
        return -1;
    }

    // connect() on a UDP socket sets the default destination for send()/recv()
    // and causes the kernel to filter out packets from other sources.
    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        std::fprintf(stderr, "[error] connect() failed: %m\n");
        close(fd);
        return -1;
    }

    return fd;
}