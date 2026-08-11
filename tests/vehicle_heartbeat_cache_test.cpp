#include "vehicle_heartbeat_cache.h"

#include <arpa/inet.h>

#include <cassert>
#include <chrono>
#include <thread>

namespace {

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
  VehicleHeartbeatCache cache(20ms);
  const auto first_address = vehicleAddress(7006);
  const auto restarted_address = vehicleAddress(7016);

  // 未收到心跳时，车辆列表为空
  const auto initial = cache.vehicleStatusList();
  assert(initial.empty());

  // 首次心跳动态发现车辆；在线期间只接受严格递增序号
  assert(!cache.vehicleAddress("truck_01"));
  assert(!cache.updateHeartbeat("", first_address, 1));
  assert(cache.updateHeartbeat("truck_02", first_address, 3));
  assert(cache.updateHeartbeat("truck_01", first_address, 8));

  const auto discovered = cache.vehicleStatusList();
  assert(discovered.size() == 2);
  assert(discovered[0].id == "truck_01");
  assert(discovered[0].online);
  assert(discovered[1].id == "truck_02");
  assert(discovered[1].online);

  assert(cache.matchesVehicleAddress("truck_01", first_address));
  assert(!cache.matchesVehicleAddress("truck_01", restarted_address));
  assert(!cache.updateHeartbeat("truck_01", restarted_address, 7));
  assert(cache.matchesVehicleAddress("truck_01", first_address));
  assert(cache.isFresh("truck_01"));
  assert(!cache.isFresh("missing"));

  // 心跳超时后允许车辆以新端点和新序号周期重新上线
  std::this_thread::sleep_for(25ms);
  assert(!cache.isFresh("truck_01"));
  assert(cache.updateHeartbeat("truck_01", restarted_address, 1));
  assert(!cache.updateHeartbeat("truck_01", restarted_address, 1));
  assert(cache.isFresh("truck_01"));

  const auto address = cache.vehicleAddress("truck_01");
  assert(address);
  assert(ntohs(address->sin_port) == 7016);
  assert(!cache.vehicleAddress("missing"));
}
