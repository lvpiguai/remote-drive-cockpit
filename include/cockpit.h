#pragma once

#include <netinet/in.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "control_command_generator.h"
#include "control_command_sender.h"
#include "input_device_reader.h"
#include "remote_drive.pb.h"
#include "vehicle_heartbeat_cache.h"
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

  // 初始化通信
  bool initialize();

  // 接收车辆数据
  void receiveVehiclePackets();

  // 处理车辆心跳
  void handleHeartbeat(const std::string &vehicle_id, std::uint32_t sequence,
                       const sockaddr_in &source);

  // 处理车辆状态
  void handleState(const remote_drive::protocol::ChassisState &state,
                   std::uint32_t sequence, const sockaddr_in &source);

  // 接收 Web 页面的车辆选择消息
  void receiveWebMessages();

  // 从 Linux evdev 设备读取完整输入帧
  void receiveInputDevice();

  // 选择页面指定的车辆
  void selectVehicle(const std::string &vehicle_id);

  // 退出并清理当前车辆选择
  void deselectVehicle(const char *reason = "取消车辆选择");

  // 不向车辆发送退出指令，直接清理已失效的本地选择
  void clearVehicleSelection(const char *reason);

  // 检查链路健康并推进控制发送
  void updateControl(Clock::time_point now);

  // 向指定车辆发送控制指令
  bool sendControlCommand(
      const std::string &vehicle_id,
      remote_drive::protocol::RemoteDriveControlCommand command);

  // 周期推送车辆在线状态
  void publishVehicleList(Clock::time_point now);

  // 更新 Web 控制页连接
  void updateWebConnectionState();

  WebSocketServer web_server_;                     // Web 控制页服务
  input_device::InputDeviceReader input_device_reader_;  // 输入设备读取器
  UdpChannel udp_channel_;                         // 车辆 UDP 通道
  ControlCommandGenerator command_generator_;      // 控制指令生成
  ControlCommandSender command_sender_;            // 控制指令发送
  VehicleHeartbeatCache heartbeat_cache_;          // 心跳缓存
  VehicleStateCache state_cache_;                  // 状态缓存
  std::optional<std::string> selected_vehicle_id_; // 当前选择车辆
  std::string cockpit_id_;                         // 驾驶舱实例 ID
  std::uint16_t vehicle_udp_port_;                 // 车辆通信端口
  std::uint16_t websocket_port_;                   // Web 控制页端口
  bool web_connected_ = false;                     // Web 连接状态
  Clock::time_point last_control_sent_{};          // 控制发送时间
  Clock::time_point last_vehicle_list_sent_{};     // 列表推送时间
};
