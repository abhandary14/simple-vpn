#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "crypto.h"
#include "loop.h"
#include "udp.h"

static bool readable_within(int fd, int timeout_ms)
{
    struct pollfd pfd{fd, POLLIN, 0};
    return poll(&pfd, 1, timeout_ms) == 1 && (pfd.revents & POLLIN);
}

static int bound_udp_socket(uint16_t &port_out)
{
    int fd = udp_server_socket(0);
    assert(fd >= 0);

    struct sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);
    assert(getsockname(fd, reinterpret_cast<struct sockaddr *>(&addr), &addr_len) == 0);
    port_out = ntohs(addr.sin_port);
    return fd;
}

static void stop_child(pid_t child, int wake_fd)
{
    assert(kill(child, SIGINT) == 0);
    const uint8_t wake = 0;
    send(wake_fd, &wake, sizeof(wake), 0);

    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
}

static void test_client_mode(const uint8_t key[KEY_LEN])
{
    int tun_pair[2];
    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, tun_pair) == 0);

    uint16_t server_port = 0;
    int server_fd = bound_udp_socket(server_port);
    int client_fd = udp_client_socket("127.0.0.1", server_port);
    assert(client_fd >= 0);

    pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        close(tun_pair[0]);
        close(server_fd);
        run_loop(tun_pair[1], client_fd, key, false);
        _exit(0);
    }

    close(tun_pair[1]);
    close(client_fd);

    const uint8_t outbound[] = {0x45, 0x00, 0x00, 0x04};
    assert(send(tun_pair[0], outbound, sizeof(outbound), 0) ==
           static_cast<ssize_t>(sizeof(outbound)));
    assert(readable_within(server_fd, 1000));

    uint8_t datagram[256];
    struct sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    ssize_t datagram_len = recvfrom(server_fd, datagram, sizeof(datagram), 0,
                                    reinterpret_cast<struct sockaddr *>(&client_addr),
                                    &client_addr_len);
    assert(datagram_len == static_cast<ssize_t>(CRYPTO_OVERHEAD + sizeof(outbound)));

    uint8_t decrypted[256];
    assert(aead_decrypt(key, datagram, datagram + NONCE_LEN, datagram + CRYPTO_OVERHEAD,
                        sizeof(outbound), decrypted));
    assert(std::memcmp(decrypted, outbound, sizeof(outbound)) == 0);

    const uint8_t malformed[] = {1, 2, 3};
    assert(sendto(server_fd, malformed, sizeof(malformed), 0,
                  reinterpret_cast<struct sockaddr *>(&client_addr), client_addr_len) ==
           static_cast<ssize_t>(sizeof(malformed)));

    const uint8_t inbound[] = {0x45, 0x01, 0x02, 0x03, 0x04};
    uint8_t encrypted[CRYPTO_OVERHEAD + sizeof(inbound)];
    assert(aead_encrypt(key, inbound, sizeof(inbound), encrypted, encrypted + NONCE_LEN,
                        encrypted + CRYPTO_OVERHEAD));
    encrypted[NONCE_LEN] ^= 0x01;
    assert(sendto(server_fd, encrypted, sizeof(encrypted), 0,
                  reinterpret_cast<struct sockaddr *>(&client_addr), client_addr_len) ==
           static_cast<ssize_t>(sizeof(encrypted)));

    assert(aead_encrypt(key, inbound, sizeof(inbound), encrypted, encrypted + NONCE_LEN,
                        encrypted + CRYPTO_OVERHEAD));
    assert(sendto(server_fd, encrypted, sizeof(encrypted), 0,
                  reinterpret_cast<struct sockaddr *>(&client_addr), client_addr_len) ==
           static_cast<ssize_t>(sizeof(encrypted)));
    assert(readable_within(tun_pair[0], 1000));

    uint8_t received[256];
    ssize_t received_len = recv(tun_pair[0], received, sizeof(received), 0);
    assert(received_len == static_cast<ssize_t>(sizeof(inbound)));
    assert(std::memcmp(received, inbound, sizeof(inbound)) == 0);

    stop_child(child, tun_pair[0]);
    close(tun_pair[0]);
    close(server_fd);
}

static void test_server_peer_learning(const uint8_t key[KEY_LEN])
{
    int tun_pair[2];
    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, tun_pair) == 0);

    uint16_t server_port = 0;
    int server_fd = bound_udp_socket(server_port);
    int peer_fd = udp_client_socket("127.0.0.1", server_port);
    assert(peer_fd >= 0);

    pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        close(tun_pair[0]);
        close(peer_fd);
        run_loop(tun_pair[1], server_fd, key, true);
        _exit(0);
    }

    close(tun_pair[1]);
    close(server_fd);

    const uint8_t before_peer[] = {0x45, 0xaa};
    assert(send(tun_pair[0], before_peer, sizeof(before_peer), 0) ==
           static_cast<ssize_t>(sizeof(before_peer)));
    assert(!readable_within(peer_fd, 150));

    const uint8_t inbound[] = {0x45, 0xbb, 0xcc};
    uint8_t encrypted[CRYPTO_OVERHEAD + sizeof(inbound)];
    assert(aead_encrypt(key, inbound, sizeof(inbound), encrypted, encrypted + NONCE_LEN,
                        encrypted + CRYPTO_OVERHEAD));
    assert(send(peer_fd, encrypted, sizeof(encrypted), 0) ==
           static_cast<ssize_t>(sizeof(encrypted)));
    assert(readable_within(tun_pair[0], 1000));

    uint8_t received[256];
    assert(recv(tun_pair[0], received, sizeof(received), 0) ==
           static_cast<ssize_t>(sizeof(inbound)));
    assert(std::memcmp(received, inbound, sizeof(inbound)) == 0);

    const uint8_t outbound[] = {0x45, 0xdd, 0xee, 0xff};
    assert(send(tun_pair[0], outbound, sizeof(outbound), 0) ==
           static_cast<ssize_t>(sizeof(outbound)));
    assert(readable_within(peer_fd, 1000));

    ssize_t datagram_len = recv(peer_fd, received, sizeof(received), 0);
    assert(datagram_len == static_cast<ssize_t>(CRYPTO_OVERHEAD + sizeof(outbound)));
    uint8_t decrypted[256];
    assert(aead_decrypt(key, received, received + NONCE_LEN, received + CRYPTO_OVERHEAD,
                        sizeof(outbound), decrypted));
    assert(std::memcmp(decrypted, outbound, sizeof(outbound)) == 0);

    stop_child(child, tun_pair[0]);
    close(tun_pair[0]);
    close(peer_fd);
}

int main()
{
    uint8_t key[KEY_LEN];
    for (size_t i = 0; i < KEY_LEN; ++i)
        key[i] = static_cast<uint8_t>(i + 1);

    test_client_mode(key);
    test_server_peer_learning(key);
    std::printf("Core loop integration tests passed.\n");
    return 0;
}
