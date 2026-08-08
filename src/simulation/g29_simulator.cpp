#include "simulation/g29_simulator.h"

#include "simulation/virtual_g29_report.h"

#include <poll.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

// 保存可选的虚拟输入设备路径输出文件
G29Simulator::G29Simulator(std::string event_path_file,
                           std::uint16_t websocket_port)
    : event_path_file_(std::move(event_path_file)),
      websocket_port_(websocket_port) {}

// 创建虚拟设备和 WebSocket 服务，并持续转发 Web 输入报告
int G29Simulator::run() {
  // uinput 创建成功后，Linux 才会生成供驾驶舱读取的 eventX
  if (!input_device_simulator_.start()) {
    std::cerr << "创建 G29 虚拟设备失败：" << input_device_simulator_.error()
              << '\n';
    return 1;
  }

  // 一键启动脚本通过该文件取得动态生成的 eventX 路径
  if (!event_path_file_.empty()) {
    std::ofstream path_file(event_path_file_, std::ios::trunc);
    if (!path_file ||
        !(path_file << input_device_simulator_.eventPath() << '\n')) {
      std::cerr << "写入设备路径文件失败：" << event_path_file_ << '\n';
      return 1;
    }
  }

  // Web 页面只连接模拟器端口，不直接接触 Linux 输入设备
  if (!web_server_.startListening(websocket_port_))
    return 1;

  std::cout << "G29 模拟器已启动，WebSocket：ws://127.0.0.1:" << websocket_port_
            << "，虚拟输入设备：" << input_device_simulator_.eventPath()
            << '\n';

  bool was_connected = false;
  bool has_logged_write_error = false;
  while (true) {
    // 同时轮询监听 socket 和当前单个 Web 客户端
    pollfd descriptors[] = {
        {web_server_.listenerFd(), POLLIN, 0},
        {web_server_.clientFd(), POLLIN, 0},
    };
    const int result = poll(descriptors, 2, 100);
    if (result < 0) {
      perror("g29 simulator poll");
      return 1;
    }

    if (descriptors[0].revents & POLLIN)
      web_server_.acceptClient();
    if (descriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) {
      // 每条合法 JSON 都转换为一帧完整的虚拟设备报告
      web_server_.receiveMessages([&](const std::string &message) {
        VirtualG29Report report{};
        if (!parseVirtualG29Report(message, report))
          return;
        if (!input_device_simulator_.writeReport(report)) {
          if (!has_logged_write_error) {
            std::cerr << "写入 G29 虚拟设备失败："
                      << input_device_simulator_.error() << '\n';
            has_logged_write_error = true;
          }
        } else {
          has_logged_write_error = false;
        }
      });
    }

    // 页面断开时释放全部输入，避免残留按键或踏板状态
    const bool connected = web_server_.connected();
    if (connected != was_connected) {
      if (connected) {
        std::cout << "G29 Web 面板已连接\n";
      } else {
        input_device_simulator_.writeReport(VirtualG29Report{});
        std::cout << "G29 Web 面板已断开，输入已复位\n";
      }
      was_connected = connected;
    }
  }
}
