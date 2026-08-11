#include "udp_channel.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>

int main() {
  UdpChannel channel;
  assert(channel.bindPort(0));
  assert(!channel.bindPort(0));

  sockaddr_in channel_address{};
  socklen_t channel_address_size = sizeof(channel_address);
  assert(getsockname(channel.fd(),
                     reinterpret_cast<sockaddr *>(&channel_address),
                     &channel_address_size) == 0);
  channel_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  const int peer_fd = socket(AF_INET, SOCK_DGRAM, 0);
  assert(peer_fd >= 0);

  constexpr char request[] = "vehicle-state";
  assert(sendto(peer_fd, request, sizeof(request), 0,
                reinterpret_cast<const sockaddr *>(&channel_address),
                sizeof(channel_address)) ==
         static_cast<ssize_t>(sizeof(request)));

  pollfd channel_poll{channel.fd(), POLLIN, 0};
  assert(poll(&channel_poll, 1, 1000) == 1);

  const auto datagram = channel.receive();
  assert(datagram);
  assert(datagram->payload.size() == sizeof(request));
  assert(std::memcmp(datagram->payload.data(), request, sizeof(request)) == 0);

  constexpr char response[] = "control-command";
  assert(channel.send(datagram->source, response, sizeof(response)));

  pollfd peer_poll{peer_fd, POLLIN, 0};
  assert(poll(&peer_poll, 1, 1000) == 1);

  char received[32]{};
  assert(recv(peer_fd, received, sizeof(received), 0) ==
         static_cast<ssize_t>(sizeof(response)));
  assert(std::memcmp(received, response, sizeof(response)) == 0);

  close(peer_fd);
}
