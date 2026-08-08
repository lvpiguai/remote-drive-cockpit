#include "cockpit/vehicle_state_cache.h"

#include <cassert>
#include <chrono>
#include <limits>
#include <thread>

namespace {
namespace pb = remote_drive::protocol;

// 构造带车辆标识和驾驶模式的最小状态快照
pb::ChassisState state(const char *vehicle_id, pb::DriveMode mode) {
  pb::ChassisState result;
  result.set_vehicle_id(vehicle_id);
  result.set_drive_mode(mode);
  return result;
}

} // namespace

int main() {
  using namespace std::chrono_literals;
  VehicleStateCache cache;
  const auto first = state("truck_01", pb::DRIVE_MODE_STANDBY);
  const auto second = state("truck_02", pb::DRIVE_MODE_REMOTE);

  assert(VehicleStateCache::isValidState(first));
  auto invalid = first;
  invalid.clear_vehicle_id();
  assert(!VehicleStateCache::isValidState(invalid));
  invalid = first;
  invalid.set_speed(std::numeric_limits<double>::infinity());
  assert(!VehicleStateCache::isValidState(invalid));

  // 不同车辆的状态、序号和接收时间分别保存  assert(!cache.record("truck_01"));
  const auto before_update = std::chrono::steady_clock::now();
  cache.update("truck_01", first, 8);
  const auto after_update = std::chrono::steady_clock::now();
  cache.update("truck_02", second, 3);

  const auto *first_record = cache.record("truck_01");
  assert(first_record);
  assert(first_record->sequence == 8);
  assert(first_record->state.drive_mode() == pb::DRIVE_MODE_STANDBY);
  assert(first_record->last_update_time >= before_update);
  assert(first_record->last_update_time <= after_update);
  assert(cache.record("truck_02"));
  assert(cache.isFresh("truck_01"));
  assert(!cache.isFresh("missing"));

  // 超过状态有效期后，记录仍可查询但不再视为新鲜
  std::this_thread::sleep_for(510ms);
  assert(!cache.isFresh("truck_01"));
}
