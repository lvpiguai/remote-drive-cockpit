#pragma once

#include <string>

// 车辆在线状态
struct VehicleOnlineStatus {
  std::string id;       // 车辆 ID
  bool online = false;  // 在线状态
};
