#include "cockpit/control_command_sender.h"

#include "protocol/udp_protocol.h"

// 分配控制序号并发送编码后的控制数据报
std::optional<std::uint32_t> ControlCommandSender::send(
    const remote_drive::protocol::RemoteDriveControlCommand &command,
    const sockaddr_in &destination) {
  const std::uint32_t sequence = next_sequence_++;
  const auto packet = remote_protocol::encodeControlCommand(command, sequence);
  if (!channel_.send(destination, packet.data(), packet.size())) {
    return std::nullopt;
  }
  return sequence;
}
