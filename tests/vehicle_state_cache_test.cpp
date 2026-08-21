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

// 构造测试车辆地址
sockaddr_in vehicleAddress(const char *ip) {
  sockaddr_in result{};
  result.sin_family = AF_INET;
  assert(inet_pton(AF_INET, ip, &result.sin_addr) == 1);
  result.sin_port = htons(7006);
  return result;
}

} // namespace

int main() {
  using namespace std::chrono_literals;
  auto first = state("truck_01", pb::DRIVE_MODE_STANDBY);
  first.set_cockpit_id("cockpit_01");
  const auto second = state("truck_02", pb::DRIVE_MODE_REMOTE);
  const auto first_address = vehicleAddress("127.0.0.1");
  const auto second_address = vehicleAddress("127.0.0.2");
  const auto unknown_address = vehicleAddress("127.0.0.3");
  VehicleStateCache cache(
      20ms, {{"truck_01", first_address}, {"truck_02", second_address}});

  // 未收到状态时已配置车辆均为离线
  assert(!cache.record("truck_01"));
  assert(cache.cockpitId("truck_01").empty());
  const auto configured = cache.vehicleStatusList();
  assert(configured.size() == 2);
  assert(configured[0].id == "truck_01");
  assert(!configured[0].online);
  assert(configured[1].id == "truck_02");
  assert(!configured[1].online);

  // 只接受车辆标识与配置来源匹配的状态
  assert(!cache.update(first, 1, unknown_address));
  assert(!cache.update(state("unknown", pb::DRIVE_MODE_STANDBY), 1,
                       unknown_address));
  const auto before_update = std::chrono::steady_clock::now();
  assert(cache.update(first, 8, first_address));
  const auto after_update = std::chrono::steady_clock::now();
  assert(cache.update(second, 3, second_address));

  const auto *first_record = cache.record("truck_01");
  assert(first_record);
  assert(first_record->sequence == 8);
  assert(first_record->state.drive_mode() == pb::DRIVE_MODE_STANDBY);
  assert(cache.cockpitId("truck_01") == "cockpit_01");
  assert(first_record->last_update_time >= before_update);
  assert(first_record->last_update_time <= after_update);
  assert(cache.record("truck_02"));
  assert(cache.isOnline("truck_01"));
  assert(!cache.isOnline("missing"));

  const auto online = cache.vehicleStatusList();
  assert(online.size() == 2);
  assert(online[0].id == "truck_01");
  assert(online[0].online);
  assert(online[1].id == "truck_02");
  assert(online[1].online);

  const auto address = cache.vehicleAddress("truck_01");
  assert(address);
  assert(ntohs(address->sin_port) == 7006);
  assert(!cache.vehicleAddress("missing"));

  // 在线期间拒绝重复序号和错误来源
  assert(!cache.update(first, 8, first_address));
  assert(!cache.update(first, 9, unknown_address));

  // 在线超时后允许配置来源以新序号重新上线
  std::this_thread::sleep_for(25ms);
  assert(!cache.isOnline("truck_01"));
  assert(!cache.update(first, 1, unknown_address));
  assert(cache.update(first, 1, first_address));
  assert(cache.isOnline("truck_01"));
  assert(ntohs(cache.vehicleAddress("truck_01")->sin_port) == 7006);
}
