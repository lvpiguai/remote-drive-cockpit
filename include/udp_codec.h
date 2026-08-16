#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "remote_drive.pb.h"

namespace udp_codec {

using PacketBytes = std::vector<std::uint8_t>;

// 将驾驶舱控制指令封装并序列化为 UDP 负载
PacketBytes encodeControlCommand(
    const remote_drive::protocol::ControlCommand &command,
    std::uint32_t sequence);

// 从 UDP 负载解码并校验一个协议包
std::optional<remote_drive::protocol::UdpPacket>
decodePacket(const std::uint8_t *data, std::size_t size);

} // namespace udp_codec
