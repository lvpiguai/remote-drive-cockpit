#pragma once

#include <netinet/in.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "remote_drive.pb.h"

// 车辆在线状态
struct VehicleOnlineStatus {
  std::string id;       // 车辆 ID
  bool online = false;  // 在线状态
};

// 单台车辆最近一次合法状态及其接收时间
struct VehicleStateRecord {
  remote_drive::protocol::VehicleState state{};
  sockaddr_in vehicle_address{};
  std::uint32_t sequence = 0;
  std::chrono::steady_clock::time_point last_update_time{};
};

// 按车辆保存最新状态、通信地址和在线时间
class VehicleStateCache {
public:
  using Clock = std::chrono::steady_clock;

  explicit VehicleStateCache(Clock::duration online_timeout);

  // 保存序号递增且来源可信的车辆状态
  bool update(const remote_drive::protocol::VehicleState &state,
              std::uint32_t sequence, const sockaddr_in &vehicle_address);

  // 查找指定车辆的最新状态记录
  const VehicleStateRecord *record(const std::string &vehicle_id) const;

  // 判断指定车辆是否仍在线
  bool isOnline(const std::string &vehicle_id) const;

  // 获取车辆最近一次状态包的来源地址
  std::optional<sockaddr_in> vehicleAddress(
      const std::string &vehicle_id) const;

  // 获取全部车辆在线状态
  std::vector<VehicleOnlineStatus> vehicleStatusList() const;

private:
  Clock::duration online_timeout_;
  std::unordered_map<std::string, VehicleStateRecord> state_records_;
};
