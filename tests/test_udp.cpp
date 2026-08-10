#include <cassert>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "udp.h"

int main()
{
    int server_fd = udp_server_socket(0);
    assert(server_fd >= 0);

    struct sockaddr_in server_addr{};
    socklen_t server_addr_len = sizeof(server_addr);
    assert(getsockname(server_fd, reinterpret_cast<struct sockaddr *>(&server_addr),
                       &server_addr_len) == 0);
    uint16_t port = ntohs(server_addr.sin_port);
    assert(port != 0);

    int client_fd = udp_client_socket("127.0.0.1", port);
    assert(client_fd >= 0);

    const char request[] = "client to server";
    assert(send(client_fd, request, sizeof(request), 0) == static_cast<ssize_t>(sizeof(request)));

    char buffer[64]{};
    struct sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    ssize_t received = recvfrom(server_fd, buffer, sizeof(buffer), 0,
                                reinterpret_cast<struct sockaddr *>(&client_addr),
                                &client_addr_len);
    assert(received == static_cast<ssize_t>(sizeof(request)));
    assert(std::memcmp(buffer, request, sizeof(request)) == 0);

    const char response[] = "server to client";
    assert(sendto(server_fd, response, sizeof(response), 0,
                  reinterpret_cast<struct sockaddr *>(&client_addr), client_addr_len) ==
           static_cast<ssize_t>(sizeof(response)));
    received = recv(client_fd, buffer, sizeof(buffer), 0);
    assert(received == static_cast<ssize_t>(sizeof(response)));
    assert(std::memcmp(buffer, response, sizeof(response)) == 0);

    assert(udp_client_socket("not-an-ip", port) == -1);

    close(client_fd);
    close(server_fd);
    std::printf("UDP loopback tests passed.\n");
    return 0;
}
