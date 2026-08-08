#pragma once

#include <cstdint>
#include <string>

#include "simulation/virtual_g29_report.h"

// 本地模拟器：将 Web 数据写入 uinput，创建虚拟输入设备
class InputDeviceSimulator {
 public:
  InputDeviceSimulator() = default;
  ~InputDeviceSimulator();

  InputDeviceSimulator(const InputDeviceSimulator &) = delete;
  InputDeviceSimulator &operator=(const InputDeviceSimulator &) = delete;

  // 创建 uinput 虚拟设备并取得对应 eventX
  bool start();

  // 将一帧 Web 数据写成 Linux 输入事件
  bool writeReport(const VirtualG29Report &report);
  const std::string &eventPath() const { return event_path_; }
  const std::string &error() const { return error_; }

 private:
  // 向 /dev/uinput 写入单个 input_event
  bool emit(std::uint16_t type, std::uint16_t code, std::int32_t value);

  // 根据 uinput sysname 查找内核创建的 eventX
  bool findEventPath(const std::string &sysname);

  // 销毁虚拟设备并关闭文件描述符
  void stop();

  int fd_ = -1;
  std::string event_path_;
  std::string error_;
};
