#include "cockpit/control_command_sender.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>

#include <cassert>

#include "protocol/udp_codec.h"

namespace {

// 获取测试通道绑定的回环地址和动态端口
sockaddr_in localAddress(const UdpChannel &channel) {
  sockaddr_in address{};
  socklen_t address_size = sizeof(address);
  assert(getsockname(channel.fd(), reinterpret_cast<sockaddr *>(&address),
                     &address_size) == 0);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return address;
}

// 等待测试 UDP 通道出现一条可读数据报
void waitUntilReadable(const UdpChannel &channel) {
  pollfd descriptor{channel.fd(), POLLIN, 0};
  assert(poll(&descriptor, 1, 1000) == 1);
}

} // namespace

int main() {
  namespace pb = remote_drive::protocol;

  UdpChannel sender_channel;
  UdpChannel receiver_channel;
  assert(sender_channel.bindPort(0));
  assert(receiver_channel.bindPort(0));

  ControlCommandSender sender(sender_channel);
  pb::RemoteDriveControlCommand command;
  command.set_cockpit_id("cockpit_01");
  command.set_remote_mode(pb::REMOTE_MODE_ENTER);
  command.set_steering_angle(12.5);

  // 首条控制指令使用序号 1，并能由接收端完整解码
  const sockaddr_in destination = localAddress(receiver_channel);
  const auto first_sequence = sender.send(command, destination);
  assert(first_sequence && *first_sequence == 1);

  waitUntilReadable(receiver_channel);
  UdpDatagram datagram;
  assert(receiver_channel.receive(datagram));

  const auto decoded = udp_codec::decodePacket(datagram.payload.data(),
                                               datagram.payload.size());
  assert(decoded);
  assert(decoded->body_case() == pb::UdpPacket::kControl);
  assert(decoded->sequence() == 1);
  assert(decoded->control().remote_mode() == pb::REMOTE_MODE_ENTER);
  assert(decoded->control().steering_angle() == 12.5);

  // 后续发送沿用同一发送器并递增控制序号
  const auto second_sequence = sender.send(command, destination);
  assert(second_sequence && *second_sequence == 2);
}
