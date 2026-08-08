#include "cockpit/vehicle_heartbeat_cache.h"

#include <arpa/inet.h>

#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

namespace {

// 构造使用回环地址的测试 UDP 端点
sockaddr_in endpoint(std::uint16_t port) {
  sockaddr_in result{};
  result.sin_family = AF_INET;
  result.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  result.sin_port = htons(port);
  return result;
}

} // namespace

int main() {
  using namespace std::chrono_literals;
  VehicleHeartbeatCache cache(std::vector<std::string>{"truck_02", "truck_01"},
                              20ms);
  const auto first_endpoint = endpoint(7006);
  const auto restarted_endpoint = endpoint(7016);

  // 未收到心跳时，配置车辆保持离线并按 ID 排序
  const auto initial = cache.vehicleStatusList();
  assert(initial.size() == 2);
  assert(initial[0].id == "truck_01");
  assert(!initial[0].online);
  assert(initial[1].id == "truck_02");
  assert(!initial[1].online);

  // 在线期间只接受严格递增序号，并保持首次确认的通信端点  assert(!cache.endpoint("truck_01"));
  assert(!cache.updateHeartbeat("missing", first_endpoint, 1));
  assert(cache.updateHeartbeat("truck_01", first_endpoint, 8));
  assert(cache.matchesEndpoint("truck_01", first_endpoint));
  assert(!cache.matchesEndpoint("truck_01", restarted_endpoint));
  assert(!cache.updateHeartbeat("truck_01", restarted_endpoint, 7));
  assert(cache.matchesEndpoint("truck_01", first_endpoint));
  assert(cache.isFresh("truck_01"));
  assert(!cache.isFresh("missing"));

  // 心跳超时后允许车辆以新端点和新序号周期重新上线
  std::this_thread::sleep_for(25ms);
  assert(!cache.isFresh("truck_01"));
  assert(cache.updateHeartbeat("truck_01", restarted_endpoint, 1));
  assert(!cache.updateHeartbeat("truck_01", restarted_endpoint, 1));
  assert(cache.isFresh("truck_01"));

  const auto address = cache.endpoint("truck_01");
  assert(address);
  assert(ntohs(address->sin_port) == 7016);
  assert(!cache.endpoint("missing"));
}
