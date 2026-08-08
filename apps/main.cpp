#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cockpit/cockpit.h"

namespace {

constexpr std::uint16_t kDefaultVehicleUdpPort = 7005;
constexpr std::uint16_t kDefaultWebSocketPort = 8765;
constexpr std::string_view kDefaultCockpitId = "cockpit_01";

// 将十进制文本解析为有效的非零端口号
bool parsePort(std::string_view text, std::uint16_t &port) {
  unsigned int value = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      value == 0 || value > 65535) {
    return false;
  }
  port = static_cast<std::uint16_t>(value);
  return true;
}

// 解析驾驶舱实例、监听端口、输入设备和可选车辆列表
bool parseArguments(int argc, char *argv[], std::string &input_device_path,
                    std::vector<std::string> &vehicle_ids,
                    std::string &cockpit_id, std::uint16_t &vehicle_udp_port,
                    std::uint16_t &websocket_port) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--cockpit-id") {
      if (++index >= argc)
        return false;
      cockpit_id = argv[index];
    } else if (argument == "--vehicle-udp-port" ||
               argument == "--websocket-port") {
      if (++index >= argc)
        return false;
      std::uint16_t &port =
          argument == "--vehicle-udp-port" ? vehicle_udp_port : websocket_port;
      if (!parsePort(argv[index], port))
        return false;
    } else if (argument.rfind("--", 0) == 0) {
      return false;
    } else if (input_device_path.empty()) {
      input_device_path = argument;
    } else {
      vehicle_ids.emplace_back(argument);
    }
  }
  return !input_device_path.empty() && !vehicle_ids.empty() &&
         !cockpit_id.empty() &&
         cockpit_id.size() < sizeof(RemoteCtlCmd{}.cockpit_id) &&
         cockpit_id.find_first_of("\\\"") == std::string::npos;
}

} // namespace

// 解析启动配置并运行驾驶舱进程
int main(int argc, char *argv[]) {
  std::string input_device_path;
  std::vector<std::string> vehicle_ids;
  std::string cockpit_id(kDefaultCockpitId);
  std::uint16_t vehicle_udp_port = kDefaultVehicleUdpPort;
  std::uint16_t websocket_port = kDefaultWebSocketPort;
  if (!parseArguments(argc, argv, input_device_path, vehicle_ids, cockpit_id,
                      vehicle_udp_port, websocket_port)) {
    std::cerr << "用法：" << argv[0]
              << " [--cockpit-id id] [--vehicle-udp-port port]"
                 " [--websocket-port port]"
                 " /dev/input/eventX vehicle_id...\n";
    return 1;
  }

  Cockpit cockpit(std::move(cockpit_id), std::move(input_device_path),
                  std::move(vehicle_ids), vehicle_udp_port, websocket_port);
  return cockpit.run();
}
