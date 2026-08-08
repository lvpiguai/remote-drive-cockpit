#include "cockpit/vehicle_state_cache.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>

namespace {

// 构造带车辆标识和驾驶模式的最小状态快照
RemoteDrivingState state(const char *vehicle_id, DriveMode mode) {
  RemoteDrivingState result{};
  std::strncpy(result.vehicle_id, vehicle_id, sizeof(result.vehicle_id) - 1);
  result.remoteMode = mode;
  return result;
}

} // namespace

int main() {
  using namespace std::chrono_literals;
  VehicleStateCache cache;
  const auto first = state("truck_01", DriveMode::STANDBY);
  const auto second = state("truck_02", DriveMode::REMOTE);

  // 不同车辆的状态、序号和接收时间分别保存  assert(!cache.record("truck_01"));
  const auto before_update = std::chrono::steady_clock::now();
  cache.update("truck_01", first, 8);
  const auto after_update = std::chrono::steady_clock::now();
  cache.update("truck_02", second, 3);

  const auto *first_record = cache.record("truck_01");
  assert(first_record);
  assert(first_record->sequence == 8);
  assert(first_record->state.remoteMode == DriveMode::STANDBY);
  assert(first_record->last_update_time >= before_update);
  assert(first_record->last_update_time <= after_update);
  assert(cache.record("truck_02"));
  assert(cache.isFresh("truck_01"));
  assert(!cache.isFresh("missing"));

  // 超过状态有效期后，记录仍可查询但不再视为新鲜
  std::this_thread::sleep_for(510ms);
  assert(!cache.isFresh("truck_01"));
}
