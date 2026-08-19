#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "remote_drive.pb.h"
#include "vehicle_state_cache.h"

namespace web_protocol {

// 页面车辆选择命令，不包含设备输入
enum class CommandType {
  SELECT_VEHICLE,   // 选择指定车辆
  DESELECT_VEHICLE, // 释放当前车辆
};

// Web 命令解析结果，仅选车携带 vehicle_id
struct Command {
  CommandType type;
  std::string vehicle_id;
};

// 解析 Web 车辆选择命令
std::optional<Command> parseCommand(const std::string &message);

// 序列化 Web 展示数据
std::string serializeVehicleState(
    const remote_drive::protocol::VehicleState &state);
std::string serializeVehicleStatusList(
    const std::vector<VehicleOnlineStatus> &vehicles);

} // namespace web_protocol
