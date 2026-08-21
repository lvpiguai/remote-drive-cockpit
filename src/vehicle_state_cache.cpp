#include "vehicle_state_cache.h"

#include <algorithm>
#include <utility>

namespace {

namespace pb = remote_drive::protocol;

// 比较两个 UDP 来源端点
bool sameAddress(const sockaddr_in &left, const sockaddr_in &right) {
  return left.sin_family == right.sin_family &&
         left.sin_addr.s_addr == right.sin_addr.s_addr &&
         left.sin_port == right.sin_port;
}

} // namespace

// 保存车辆在线超时时间
VehicleStateCache::VehicleStateCache(Clock::duration online_timeout,
                                     VehicleAddressMap vehicle_addresses)
    : online_timeout_(online_timeout),
      vehicle_addresses_(std::move(vehicle_addresses)) {}

// 校验并保存已配置车辆的最新状态
bool VehicleStateCache::update(const pb::VehicleState &state,
                               std::uint32_t sequence,
                               const sockaddr_in &vehicle_address) {
  // 校验车辆标识和配置来源
  const std::string &vehicle_id = state.vehicle_id();
  const auto configured = vehicle_addresses_.find(vehicle_id);
  if (configured == vehicle_addresses_.end() ||
      !sameAddress(configured->second, vehicle_address)) {
    return false;
  }

  // 查找车辆已有状态
  const auto previous = state_records_.find(vehicle_id);

  // 在线期间拒绝未递增序号
  if (previous != state_records_.end() && isOnline(vehicle_id) &&
      sequence <= previous->second.sequence) {
    return false;
  }

  // 覆盖状态并刷新接收时间
  state_records_[vehicle_id] = {state, sequence, Clock::now()};
  return true;
}

// 查找指定车辆的最新状态记录
const VehicleStateRecord *
VehicleStateCache::record(const std::string &vehicle_id) const {
  // 未发现车辆时不创建空记录
  const auto state_iterator = state_records_.find(vehicle_id);
  return state_iterator == state_records_.end() ? nullptr
                                                 : &state_iterator->second;
}

// 根据最后一次状态更新时间判断车辆是否在线
bool VehicleStateCache::isOnline(const std::string &vehicle_id) const {
  const auto *state_record = record(vehicle_id);
  return state_record &&
         Clock::now() - state_record->last_update_time < online_timeout_;
}

// 获取当前控制驾驶舱 ID
std::string
VehicleStateCache::cockpitId(const std::string &vehicle_id) const {
  const auto *state_record = record(vehicle_id);
  return state_record ? state_record->state.cockpit_id() : std::string{};
}

std::optional<sockaddr_in>
VehicleStateCache::vehicleAddress(const std::string &vehicle_id) const {
  const auto address = vehicle_addresses_.find(vehicle_id);
  return address == vehicle_addresses_.end()
             ? std::nullopt
             : std::optional<sockaddr_in>(address->second);
}

// 生成按车辆 ID 排序的在线状态列表
std::vector<VehicleOnlineStatus> VehicleStateCache::vehicleStatusList() const {
  // 从全部已配置车辆生成当前在线快照
  std::vector<VehicleOnlineStatus> result;
  result.reserve(vehicle_addresses_.size());
  for (const auto &vehicle : vehicle_addresses_) {
    result.push_back({vehicle.first, isOnline(vehicle.first)});
  }

  // 固定输出顺序，避免前端列表抖动
  std::sort(result.begin(), result.end(),
            [](const VehicleOnlineStatus &left,
               const VehicleOnlineStatus &right) {
              return left.id < right.id;
            });
  return result;
}
