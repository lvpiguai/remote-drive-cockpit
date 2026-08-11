#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "cockpit.h"

namespace {

constexpr std::uint16_t kDefaultVehicleUdpPort = 7005;
constexpr std::uint16_t kDefaultWebSocketPort = 8765;
constexpr std::string_view kDefaultCockpitId = "cockpit_01";
constexpr std::size_t kMaxCockpitIdLength = 19;

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

// 解析命令行中的驾驶舱 ID、监听端口和输入设备路径
bool parseArguments(int argc, char *argv[], std::string &input_device_path,
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
      return false;
    }
  }
  return !input_device_path.empty() && !cockpit_id.empty() &&
         cockpit_id.size() <= kMaxCockpitIdLength &&
         cockpit_id.find_first_of("\\\"") == std::string::npos;
}

} // namespace

// 解析命令行参数并运行驾驶舱进程
int main(int argc, char *argv[]) {
  std::string input_device_path;
  std::string cockpit_id(kDefaultCockpitId);
  std::uint16_t vehicle_udp_port = kDefaultVehicleUdpPort;
  std::uint16_t websocket_port = kDefaultWebSocketPort;
  if (!parseArguments(argc, argv, input_device_path, cockpit_id,
                      vehicle_udp_port, websocket_port)) {
    std::cerr << "用法：" << argv[0]
              << " [--cockpit-id id] [--vehicle-udp-port port]"
                 " [--websocket-port port]"
                 " /dev/input/eventX\n";
    return 1;
  }

  Cockpit cockpit(std::move(cockpit_id), std::move(input_device_path),
                  vehicle_udp_port, websocket_port);
  return cockpit.run();
}
