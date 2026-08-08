#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "remote_drive.pb.h"

// 单台车辆最近一次合法状态及其接收时间
struct VehicleStateRecord {
  remote_drive::protocol::ChassisState state{};
  std::uint32_t sequence = 0;
  std::chrono::steady_clock::time_point last_update_time{};
};

// 按车辆保存最新状态记录，不依赖当前车辆选择
class VehicleStateCache {
public:
  // 保存指定车辆的最新状态记录
  void update(const std::string &vehicle_id,
              const remote_drive::protocol::ChassisState &state,
              std::uint32_t sequence);

  // 查找指定车辆的最新状态记录
  const VehicleStateRecord *record(const std::string &vehicle_id) const;

  // 判断指定车辆的状态记录是否仍在有效期内
  bool isFresh(const std::string &vehicle_id) const;

private:
  using Clock = std::chrono::steady_clock;
  static constexpr auto kTimeout = std::chrono::milliseconds(500);

  std::unordered_map<std::string, VehicleStateRecord> state_records_;
};
