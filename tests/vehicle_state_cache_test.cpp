#include "vehicle_state_cache.h"

#include <arpa/inet.h>

#include <cassert>
#include <chrono>
#include <thread>

namespace {
namespace pb = remote_drive::protocol;

// 构造带车辆标识和驾驶模式的最小状态快照
pb::VehicleState state(const char *vehicle_id, pb::DriveMode mode) {
  pb::VehicleState result;
  result.set_vehicle_id(vehicle_id);
  result.set_drive_mode(mode);
  return result;
}

// 构造使用回环地址的测试车辆地址
sockaddr_in vehicleAddress(std::uint16_t port) {
  sockaddr_in result{};
  result.sin_family = AF_INET;
  result.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  result.sin_port = htons(port);
  return result;
}

} // namespace

int main() {
  using namespace std::chrono_literals;
  VehicleStateCache cache(20ms);
  const auto first = state("truck_01", pb::DRIVE_MODE_STANDBY);
  const auto second = state("truck_02", pb::DRIVE_MODE_REMOTE);
  const auto first_address = vehicleAddress(7006);
  const auto restarted_address = vehicleAddress(7016);

  // 状态首次到达时动态发现车辆并记录来源地址
  assert(!cache.record("truck_01"));
  assert(cache.vehicleStatusList().empty());
  const auto before_update = std::chrono::steady_clock::now();
  assert(cache.update(first, 8, first_address));
  const auto after_update = std::chrono::steady_clock::now();
  assert(cache.update(second, 3, first_address));

  const auto *first_record = cache.record("truck_01");
  assert(first_record);
  assert(first_record->sequence == 8);
  assert(first_record->state.drive_mode() == pb::DRIVE_MODE_STANDBY);
  assert(first_record->last_update_time >= before_update);
  assert(first_record->last_update_time <= after_update);
  assert(cache.record("truck_02"));
  assert(cache.isOnline("truck_01"));
  assert(!cache.isOnline("missing"));

  const auto discovered = cache.vehicleStatusList();
  assert(discovered.size() == 2);
  assert(discovered[0].id == "truck_01");
  assert(discovered[0].online);
  assert(discovered[1].id == "truck_02");
  assert(discovered[1].online);

  const auto address = cache.vehicleAddress("truck_01");
  assert(address);
  assert(ntohs(address->sin_port) == 7006);
  assert(!cache.vehicleAddress("missing"));

  // 在线期间拒绝重复序号和同车辆的其他来源地址
  assert(!cache.update(first, 8, first_address));
  assert(!cache.update(first, 9, restarted_address));

  // 在线超时后允许车辆以新端点和新序号重新上线
  std::this_thread::sleep_for(25ms);
  assert(!cache.isOnline("truck_01"));
  assert(cache.update(first, 1, restarted_address));
  assert(cache.isOnline("truck_01"));
  assert(ntohs(cache.vehicleAddress("truck_01")->sin_port) == 7016);
}
