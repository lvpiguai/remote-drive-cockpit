#include "cockpit/vehicle_heartbeat_cache.h"

#include <algorithm>
#include <utility>

// 注册需要缓存心跳的车辆及在线超时时间
VehicleHeartbeatCache::VehicleHeartbeatCache(
    std::vector<std::string> vehicle_ids, Clock::duration online_timeout)
    : online_timeout_(online_timeout) {
  for (std::string &vehicle_id : vehicle_ids) {
    heartbeat_records_.emplace(std::move(vehicle_id), HeartbeatRecord{});
  }
}

// 接受新心跳并刷新车辆端点和在线时间
bool VehicleHeartbeatCache::updateHeartbeat(
    const std::string &vehicle_id, const sockaddr_in &endpoint,
    std::uint32_t sequence) {
  const auto heartbeat_iterator = heartbeat_records_.find(vehicle_id);
  if (heartbeat_iterator == heartbeat_records_.end()) return false;

  HeartbeatRecord &heartbeat_record = heartbeat_iterator->second;
  if (heartbeat_record.endpoint && isFresh(vehicle_id) &&
      sequence <= heartbeat_record.sequence) {
    return false;
  }

  heartbeat_record.endpoint = endpoint;
  heartbeat_record.sequence = sequence;
  heartbeat_record.last_update_time = Clock::now();
  return true;
}

// 判断指定车辆的最近心跳是否仍然有效
bool VehicleHeartbeatCache::isFresh(const std::string &vehicle_id) const {
  const auto heartbeat_iterator = heartbeat_records_.find(vehicle_id);
  return heartbeat_iterator != heartbeat_records_.end() &&
         heartbeat_iterator->second.endpoint &&
         Clock::now() - heartbeat_iterator->second.last_update_time <
             online_timeout_;
}

// 返回车辆最近一次心跳提供的通信端点
std::optional<sockaddr_in> VehicleHeartbeatCache::endpoint(
    const std::string &vehicle_id) const {
  const auto heartbeat_iterator = heartbeat_records_.find(vehicle_id);
  if (heartbeat_iterator == heartbeat_records_.end() ||
      !heartbeat_iterator->second.endpoint) {
    return std::nullopt;
  }
  return heartbeat_iterator->second.endpoint;
}

// 校验数据包来源是否匹配车辆心跳端点
bool VehicleHeartbeatCache::matchesEndpoint(
    const std::string &vehicle_id, const sockaddr_in &endpoint) const {
  const auto known = this->endpoint(vehicle_id);
  return known && known->sin_family == endpoint.sin_family &&
         known->sin_addr.s_addr == endpoint.sin_addr.s_addr &&
         known->sin_port == endpoint.sin_port;
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
