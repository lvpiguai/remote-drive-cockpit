#pragma once

#include <string>

// 车辆在线状态
struct VehicleOnlineStatus {
  std::string id;            // 车辆 ID
  bool online = false;       // 在线状态
  std::string controller_id; // 当前控制驾驶舱；空字符串表示空闲
  std::string drive_mode;    // 当前驾驶模式；无有效状态时为空
};
