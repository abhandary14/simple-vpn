#include "loop.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "crypto.h"

static volatile sig_atomic_t g_running = 1;

static void on_sigint(int)
{
    g_running = 0;
}

void run_loop(int tun_fd, int udp_fd, const uint8_t key[KEY_LEN], bool is_server)
{
    signal(SIGINT, on_sigint);

    struct sockaddr_in peer_addr{};
    bool peer_known = !is_server; // client already has a fixed peer via connect()

    struct pollfd fds[2] = {
        {tun_fd, POLLIN, 0},
        {udp_fd, POLLIN, 0},
    };

    uint8_t tun_buf[BUF_SIZE];
    uint8_t udp_buf[BUF_SIZE];

    while (g_running)
    {
        int n = poll(fds, 2, -1);
        if (n < 0)
        {
            if (errno == EINTR)
                break;
            std::fprintf(stderr, "[error] poll failed: %s\n", strerror(errno));
            break;
        }

        // TUN -> encrypt -> UDP
        if (fds[0].revents & POLLIN)
        {
            ssize_t pt_len = read(tun_fd, tun_buf, sizeof(tun_buf));
            if (pt_len > 0)
            {
                uint8_t out[CRYPTO_OVERHEAD + BUF_SIZE];
                if (!aead_encrypt(key, tun_buf, pt_len, out, out + NONCE_LEN,
                                  out + CRYPTO_OVERHEAD))
                {
                    std::fprintf(stderr, "[warn] dropped packet: encryption failed\n");
                }
                else
                {
                    size_t out_len = CRYPTO_OVERHEAD + pt_len;
                    if (!is_server)
                    {
                        send(udp_fd, out, out_len, 0);
                    }
                    else if (peer_known)
                    {
                        sendto(udp_fd, out, out_len, 0,
                               reinterpret_cast<struct sockaddr *>(&peer_addr), sizeof(peer_addr));
                    }
                    // else: no known peer yet, drop (FR-2.4)
                }
            }
        }

        // UDP -> decrypt -> TUN
        if (fds[1].revents & POLLIN)
        {
            struct sockaddr_in src{};
            socklen_t src_len = sizeof(src);
            ssize_t n2 = recvfrom(udp_fd, udp_buf, sizeof(udp_buf), 0,
                                  reinterpret_cast<struct sockaddr *>(&src), &src_len);
            if (n2 >= static_cast<ssize_t>(CRYPTO_OVERHEAD))
            {
                size_t ct_len = n2 - CRYPTO_OVERHEAD;
                uint8_t plaintext[BUF_SIZE];
                if (aead_decrypt(key, udp_buf, udp_buf + NONCE_LEN,
                                 udp_buf + CRYPTO_OVERHEAD, ct_len, plaintext))
                {
                    write(tun_fd, plaintext, ct_len);
                    if (is_server)
                    {
                        peer_addr = src;
                        peer_known = true;
                    }
                }
                else
                {
                    std::fprintf(stderr, "[warn] dropped packet: GCM tag verification failed\n");
                }
            }
            else if (n2 >= 0)
            {
                std::fprintf(stderr, "[warn] dropped packet: too short (%zd bytes)\n", n2);
            }
        }
    }

    close(tun_fd);
    close(udp_fd);
}
