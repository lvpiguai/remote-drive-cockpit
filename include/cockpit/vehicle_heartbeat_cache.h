#pragma once

#include <netinet/in.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cockpit/vehicle_online_status.h"

// 缓存车辆心跳并提供在线状态和通信端点
class VehicleHeartbeatCache {
 public:
  using Clock = std::chrono::steady_clock;

  VehicleHeartbeatCache(std::vector<std::string> vehicle_ids,
                        Clock::duration online_timeout);

  // 更新车辆心跳
  bool updateHeartbeat(const std::string &vehicle_id,
                       const sockaddr_in &endpoint, std::uint32_t sequence);

  // 判断指定车辆的心跳记录是否仍然新鲜
  bool isFresh(const std::string &vehicle_id) const;

  // 获取车辆通信地址
  std::optional<sockaddr_in> endpoint(const std::string &vehicle_id) const;

  // 校验车辆通信地址
  bool matchesEndpoint(const std::string &vehicle_id,
                       const sockaddr_in &endpoint) const;

  // 获取全部车辆在线状态
  std::vector<VehicleOnlineStatus> vehicleStatusList() const;

 private:
  struct HeartbeatRecord {
    std::optional<sockaddr_in> endpoint;
    std::uint32_t sequence = 0;
    Clock::time_point last_update_time{};
  };

  Clock::duration online_timeout_;
  std::unordered_map<std::string, HeartbeatRecord> heartbeat_records_;
};
