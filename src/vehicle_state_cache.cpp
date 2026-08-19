#include "vehicle_state_cache.h"

#include <algorithm>

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
VehicleStateCache::VehicleStateCache(Clock::duration online_timeout)
    : online_timeout_(online_timeout) {}

// 状态包同时用于动态发现车辆和刷新在线时间
bool VehicleStateCache::update(const pb::VehicleState &state,
                               std::uint32_t sequence,
                               const sockaddr_in &vehicle_address) {
  // 查找车辆已有状态
  const std::string &vehicle_id = state.vehicle_id();
  const auto previous = state_records_.find(vehicle_id);

  // 在线期间拒绝来源变化和未递增序号
  if (previous != state_records_.end() && isOnline(vehicle_id) &&
      (!sameAddress(previous->second.vehicle_address, vehicle_address) ||
       sequence <= previous->second.sequence)) {
    return false;
  }

  // 覆盖状态并刷新来源地址和接收时间
  state_records_[vehicle_id] = {state, vehicle_address, sequence, Clock::now()};
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

std::optional<sockaddr_in>
VehicleStateCache::vehicleAddress(const std::string &vehicle_id) const {
  // 控制指令发送到最近一次状态包的来源端点
  const auto *state_record = record(vehicle_id);
  if (!state_record)
    return std::nullopt;
  return state_record->vehicle_address;
}

// 生成按车辆 ID 排序的在线状态列表
std::vector<VehicleOnlineStatus> VehicleStateCache::vehicleStatusList() const {
  // 从全部已发现车辆生成当前在线快照
  std::vector<VehicleOnlineStatus> result;
  result.reserve(state_records_.size());
  for (const auto &state_entry : state_records_) {
    result.push_back({state_entry.first, isOnline(state_entry.first)});
  }

  // 固定输出顺序，避免前端列表抖动
  std::sort(result.begin(), result.end(),
            [](const VehicleOnlineStatus &left,
               const VehicleOnlineStatus &right) {
              return left.id < right.id;
            });
  return result;
}
