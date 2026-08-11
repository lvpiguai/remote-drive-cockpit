#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <optional>

#include "udp_channel.h"
#include "remote_drive.pb.h"

// 为控制指令分配序号、编码并通过车辆通道发送
class ControlCommandSender {
 public:
  explicit ControlCommandSender(UdpChannel &channel) : channel_(channel) {}

  // 发送一条控制指令，成功时返回本次控制序号
  std::optional<std::uint32_t>
  send(const remote_drive::protocol::RemoteDriveControlCommand &command,
       const sockaddr_in &destination);

 private:
  UdpChannel &channel_;
  std::uint32_t next_sequence_ = 1;
};
