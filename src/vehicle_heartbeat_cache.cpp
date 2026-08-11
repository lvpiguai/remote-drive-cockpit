#include "vehicle_heartbeat_cache.h"

#include <algorithm>

// 初始化车辆在线超时时间；车辆记录由心跳动态创建
VehicleHeartbeatCache::VehicleHeartbeatCache(Clock::duration online_timeout)
    : online_timeout_(online_timeout) {}

// 接受新心跳并刷新车辆地址和在线时间
bool VehicleHeartbeatCache::updateHeartbeat(
    const std::string &vehicle_id, const sockaddr_in &vehicle_address,
    std::uint32_t sequence) {
  if (vehicle_id.empty())
    return false;

  HeartbeatRecord &heartbeat_record = heartbeat_records_[vehicle_id];
  if (heartbeat_record.vehicle_address && isFresh(vehicle_id) &&
      sequence <= heartbeat_record.sequence) {
    return false;
  }

  heartbeat_record.vehicle_address = vehicle_address;
  heartbeat_record.sequence = sequence;
  heartbeat_record.last_update_time = Clock::now();
  return true;
}

// 判断指定车辆的最近心跳是否仍然有效
bool VehicleHeartbeatCache::isFresh(const std::string &vehicle_id) const {
  const auto heartbeat_iterator = heartbeat_records_.find(vehicle_id);
  return heartbeat_iterator != heartbeat_records_.end() &&
         heartbeat_iterator->second.vehicle_address &&
         Clock::now() - heartbeat_iterator->second.last_update_time <
             online_timeout_;
}

// 返回车辆最近一次心跳提供的通信地址
std::optional<sockaddr_in> VehicleHeartbeatCache::vehicleAddress(
    const std::string &vehicle_id) const {
  const auto heartbeat_iterator = heartbeat_records_.find(vehicle_id);
  if (heartbeat_iterator == heartbeat_records_.end() ||
      !heartbeat_iterator->second.vehicle_address) {
    return std::nullopt;
  }
  return heartbeat_iterator->second.vehicle_address;
}

// 校验数据包来源是否匹配车辆心跳地址
bool VehicleHeartbeatCache::matchesVehicleAddress(
    const std::string &vehicle_id, const sockaddr_in &vehicle_address) const {
  const auto known = this->vehicleAddress(vehicle_id);
  return known && known->sin_family == vehicle_address.sin_family &&
         known->sin_addr.s_addr == vehicle_address.sin_addr.s_addr &&
         known->sin_port == vehicle_address.sin_port;
}

// 生成按车辆 ID 排序的在线状态列表
std::vector<VehicleOnlineStatus>
VehicleHeartbeatCache::vehicleStatusList() const {
  std::vector<VehicleOnlineStatus> result;
  result.reserve(heartbeat_records_.size());
  for (const auto &heartbeat_entry : heartbeat_records_) {
    result.push_back({heartbeat_entry.first, isFresh(heartbeat_entry.first)});
  }
  std::sort(result.begin(), result.end(),
            [](const VehicleOnlineStatus &left,
               const VehicleOnlineStatus &right) {
              return left.id < right.id;
            });
  return result;
}
