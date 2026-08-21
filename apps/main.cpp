#include <arpa/inet.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "cockpit.h"
#include "remote_drive_cockpit_config.pb.h"

namespace {

constexpr std::uint16_t kDefaultWebSocketPort = 8765;
constexpr std::uint16_t kVehicleUdpPort = 7006;
constexpr std::size_t kMaxIdLength = 19;

// 校验协议标识字段
bool validId(const std::string &id) {
  return !id.empty() && id.size() <= kMaxIdLength &&
         id.find_first_of("\\\"") == std::string::npos;
}

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

// 解析配置路径、WebSocket 端口和输入设备路径
bool parseArguments(int argc, char *argv[], std::string &input_device_path,
                    std::string &config_path,
                    std::uint16_t &websocket_port) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--config") {
      if (++index >= argc)
        return false;
      config_path = argv[index];
    } else if (argument == "--websocket-port") {
      if (++index >= argc)
        return false;
      if (!parsePort(argv[index], websocket_port))
        return false;
    } else if (argument.rfind("--", 0) == 0) {
      return false;
    } else if (input_device_path.empty()) {
      input_device_path = argument;
    } else {
      return false;
    }
  }
  return !input_device_path.empty() && !config_path.empty();
}

// 加载驾驶舱身份和允许通信的车辆
bool loadConfig(const std::string &path, std::string &cockpit_id,
                VehicleAddressMap &vehicle_addresses) {
  std::ifstream input(path);
  if (!input)
    return false;

  remote_drive::cockpit::RemoteDriveCockpitConfig config;
  google::protobuf::io::IstreamInputStream stream(&input);
  if (!google::protobuf::TextFormat::Parse(&stream, &config) ||
      !config.IsInitialized() || !validId(config.cockpit_id()) ||
      config.vehicles().empty()) {
    return false;
  }

  VehicleAddressMap addresses;
  std::unordered_set<std::uint32_t> vehicle_ips;
  for (const auto &vehicle : config.vehicles()) {
    if (!validId(vehicle.vehicle_id()))
      return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kVehicleUdpPort);
    if (inet_pton(AF_INET, vehicle.ip().c_str(), &address.sin_addr) != 1 ||
        !addresses.emplace(vehicle.vehicle_id(), address).second ||
        !vehicle_ips.insert(address.sin_addr.s_addr).second) {
      return false;
    }
  }

  cockpit_id = config.cockpit_id();
  vehicle_addresses = std::move(addresses);
  return true;
}

} // namespace

// 解析命令行参数并运行驾驶舱进程
int main(int argc, char *argv[]) {
  std::string input_device_path;
  std::string config_path;
  std::uint16_t websocket_port = kDefaultWebSocketPort;
  if (!parseArguments(argc, argv, input_device_path, config_path,
                      websocket_port)) {
    std::cerr << "用法：" << argv[0]
              << " --config path [--websocket-port port]"
                 " /dev/input/eventX\n";
    return 1;
  }

  std::string cockpit_id;
  VehicleAddressMap vehicle_addresses;
  if (!loadConfig(config_path, cockpit_id, vehicle_addresses)) {
    std::cerr << "加载驾驶舱配置失败：" << config_path << '\n';
    return 1;
  }

  Cockpit cockpit(std::move(cockpit_id), std::move(input_device_path),
                  websocket_port, std::move(vehicle_addresses));
  return cockpit.run();
}
