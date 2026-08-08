#pragma once

#include <cstdint>
#include <string>

#include "common/websocket_server.h"
#include "simulation/input_device_simulator.h"

// G29 模拟进程：将 Web 输入写入 Linux 虚拟输入设备
class G29Simulator {
public:
  // 使用指定路径输出文件和 WebSocket 端口创建模拟进程
  G29Simulator(std::string event_path_file, std::uint16_t websocket_port);

  G29Simulator(const G29Simulator &) = delete;
  G29Simulator &operator=(const G29Simulator &) = delete;

  // 初始化虚拟设备和 WebSocket 服务并持续处理输入
  int run();

private:
  std::string event_path_file_;
  std::uint16_t websocket_port_;
  InputDeviceSimulator input_device_simulator_;
  WebSocketServer web_server_;
};
