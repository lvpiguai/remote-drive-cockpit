#pragma once

#include <netinet/in.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "control_command_generator.h"
#include "input_device_reader.h"
#include "remote_drive.pb.h"
#include "vehicle_state_cache.h"
#include "websocket_server.h"
#include "udp_channel.h"

class Cockpit {
public:
  // 使用指定输入设备和本地通信端口创建驾驶舱
  Cockpit(std::string cockpit_id, std::string input_device_path,
          std::uint16_t vehicle_udp_port, std::uint16_t websocket_port);

  // 禁止复制
  Cockpit(const Cockpit &) = delete;

  // 禁止赋值
  Cockpit &operator=(const Cockpit &) = delete;

  // 运行事件循环
  int run();

private:
  using Clock = std::chrono::steady_clock;

  // 初始化输入设备和通信资源
  bool initialize();

  // 接收车辆状态
  void receiveVehicleStates();

  // 处理一条 Web 页面消息
  void processWebMessage(const std::string &message);

  // 从 Linux evdev 设备读取完整输入帧
  void receiveInputDevice();

  // 选择页面指定的车辆
  void selectVehicle(const std::string &vehicle_id);

  // 退出并清理当前车辆选择
  void deselectVehicle();

  // 向指定车辆发送控制指令
  bool sendControlCommand(
      const std::string &vehicle_id,
      remote_drive::protocol::ControlCommand command);

  // 周期推送车辆在线状态
  void publishVehicleList(Clock::time_point now);

  WebSocketServer web_server_;                     // Web 控制页服务
  input_device::InputDeviceReader input_device_reader_;  // 输入设备读取器
  UdpChannel udp_channel_;                         // 车辆 UDP 通道
  ControlCommandGenerator command_generator_;      // 控制指令生成
  VehicleStateCache state_cache_;                  // 状态及在线缓存
  std::optional<std::string> selected_vehicle_id_; // 当前选择车辆
  std::string cockpit_id_;                         // 驾驶舱实例 ID
  std::string input_device_path_;                   // 输入设备路径
  std::uint16_t vehicle_udp_port_;                 // 车辆通信端口
  std::uint16_t websocket_port_;                   // Web 控制页端口
  Clock::time_point last_control_sent_{};          // 控制发送时间
  Clock::time_point last_vehicle_list_sent_{};     // 列表推送时间
  std::uint32_t next_control_sequence_ = 1;         // 下一控制指令序号
};
