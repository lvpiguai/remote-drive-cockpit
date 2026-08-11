#include "vehicle_state_cache.h"

#include <cmath>
#include <cstddef>

namespace {

namespace pb = remote_drive::protocol;

constexpr std::size_t kMaxIdLength = 19;

} // namespace

bool VehicleStateCache::isValidState(const pb::ChassisState &state) {
  return !state.vehicle_id().empty() &&
         state.vehicle_id().size() <= kMaxIdLength &&
         state.controller_id().size() <= kMaxIdLength &&
         std::isfinite(state.steering_angle()) &&
         std::isfinite(state.speed()) &&
         pb::DriveMode_IsValid(state.drive_mode()) &&
         pb::Gear_IsValid(state.gear()) && pb::Bucket_IsValid(state.bucket());
}

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
