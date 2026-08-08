#include "simulation/g29_simulator.h"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::uint16_t kDefaultWebSocketPort = 8766;

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

// 解析 WebSocket 端口和可选的 eventX 路径输出文件
bool parseArguments(int argc, char *argv[], std::string &event_path_file,
                    std::uint16_t &websocket_port) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--websocket-port") {
      if (++index >= argc || !parsePort(argv[index], websocket_port)) {
        return false;
      }
    } else if (argument.rfind("--", 0) == 0 || !event_path_file.empty()) {
      return false;
    } else {
      event_path_file = argument;
    }
  }
  return true;
}

} // namespace

// 解析参数并启动 G29 模拟进程
int main(int argc, char *argv[]) {
  std::string event_path_file;
  std::uint16_t websocket_port = kDefaultWebSocketPort;
  if (!parseArguments(argc, argv, event_path_file, websocket_port)) {
    std::cerr << "用法：" << argv[0]
              << " [--websocket-port port] [event-path-file]\n";
    return 1;
  }

  G29Simulator simulator(std::move(event_path_file), websocket_port);
  return simulator.run();
}
