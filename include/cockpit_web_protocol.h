#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vehicle_online_status.h"
#include "remote_drive.pb.h"

namespace web_protocol {

// Web 车辆选择命令，不包含设备输入
enum class CommandType {
  UNKNOWN,          // 无法识别或格式非法
  SELECT_VEHICLE,   // 选择指定车辆
  DESELECT_VEHICLE, // 释放当前车辆
};

// Web 命令解析结果，仅选车携带 vehicle_id
struct Command {
  CommandType type = CommandType::UNKNOWN;
  std::string vehicle_id;
};

// 解析 Web 车辆选择命令
Command parseCommand(const std::string &message);

// 序列化 Web 展示数据
std::string serializeVehicleState(
    const remote_drive::protocol::ChassisState &state);
std::string serializeVehicleStatusList(
    const std::vector<VehicleOnlineStatus> &vehicles);

} // namespace web_protocol
