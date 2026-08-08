#include "cockpit/vehicle_state_cache.h"

// 保存指定车辆的最新状态记录
void VehicleStateCache::update(const std::string &vehicle_id,
                               const remote_drive::protocol::ChassisState &state,
                               std::uint32_t sequence) {
  state_records_[vehicle_id] = {state, sequence, Clock::now()};
}

// 查找指定车辆的最新状态记录
const VehicleStateRecord *
VehicleStateCache::record(const std::string &vehicle_id) const {
  const auto state_iterator = state_records_.find(vehicle_id);
  return state_iterator == state_records_.end() ? nullptr
                                                 : &state_iterator->second;
}

// 判断指定车辆的状态记录是否仍然新鲜
bool VehicleStateCache::isFresh(const std::string &vehicle_id) const {
  const auto *state_record = record(vehicle_id);
  return state_record &&
         Clock::now() - state_record->last_update_time < kTimeout;
}
