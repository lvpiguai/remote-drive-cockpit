#include "cockpit.h"

#include <poll.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "cockpit_web_protocol.h"
#include "udp_codec.h"

namespace {

namespace pb = remote_drive::protocol;

constexpr auto kVehicleOnlineTimeout = std::chrono::milliseconds(500);
constexpr auto kVehicleListInterval = std::chrono::milliseconds(100);
constexpr auto kControlInterval = std::chrono::milliseconds(20);
constexpr int kPollTimeoutMs = 20;

} // namespace

// 创建驾驶舱并注册输入设备和本地通信端口
Cockpit::Cockpit(std::string cockpit_id, std::string input_device_path,
                 std::uint16_t vehicle_udp_port, std::uint16_t websocket_port)
    : state_cache_(kVehicleOnlineTimeout),
      cockpit_id_(std::move(cockpit_id)),
      input_device_path_(std::move(input_device_path)),
      vehicle_udp_port_(vehicle_udp_port),
      websocket_port_(websocket_port) {}

// 初始化资源并持续运行驾驶舱事件循环
int Cockpit::run() {
  // 初始化通信资源
  if (!initialize())
    return 1;

  // 持续运行事件循环
  while (true) {
    // 组装车辆通信和输入设备监听端点
    pollfd descriptors[] = {
        {udp_channel_.fd(), POLLIN, 0},
        {input_device_reader_.fd(), POLLIN, 0},
    };

    // 等待通信事件
    const int result = poll(descriptors, 2, kPollTimeoutMs);
    if (result < 0) {
      perror("cockpit poll");
      return 1;
    }

    // 处理就绪事件
    if (descriptors[0].revents & POLLIN)
      receiveVehicleStates();
    if (descriptors[1].revents & POLLIN)
      receiveInputDevice();
    if (descriptors[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
      // 输入设备异常时退出车辆，但保留页面和车辆状态服务
      deselectVehicle();
      input_device_reader_.closeDevice();
    }

    // 推进 WebSocket 事件并处理完整文本消息
    web_server_.poll();
    while (auto message = web_server_.takeMessage()) {
      processWebMessage(*message);
    }

    // Web 断开时退出当前车辆
    if (!web_server_.connected())
      deselectVehicle();

    // 使用同一时间点处理本轮周期任务
    const auto now = Clock::now();

    // 所选车辆离线时尝试退出，车端断联超时负责兜底
    if (selected_vehicle_id_ &&
        !state_cache_.isOnline(*selected_vehicle_id_)) {
      deselectVehicle();
    }

    // 车辆在线时每 20ms 生成并发送一次控制指令
    if (selected_vehicle_id_ && command_generator_.hasInput() &&
        now - last_control_sent_ >= kControlInterval) {
      const pb::ControlCommand command = command_generator_.generate(now);
      if (sendControlCommand(*selected_vehicle_id_, command))
        last_control_sent_ = now;
    }

    // 每 100ms 向 Web 推送完整车辆列表
    if (now - last_vehicle_list_sent_ >= kVehicleListInterval) {
      publishVehicleList(now);
    }
  }
}

// 初始化输入设备、WebSocket 监听和 UDP 通信端点
bool Cockpit::initialize() {
  // 打开输入设备
  if (!input_device_reader_.openDevice(input_device_path_)) {
    std::cerr << "打开输入设备失败：" << input_device_reader_.error() << '\n';
    return false;
  }

  // 创建并启动 Web 控制页监听端点
  if (!web_server_.startListening(websocket_port_))
    return false;

  // 创建车辆通信端点
  if (!udp_channel_.bindPort(vehicle_udp_port_)) {
    perror("cockpit socket");
    return false;
  }
  return true;
}

// 接收并处理车辆状态
void Cockpit::receiveVehicleStates() {
  // 读取并解码所有待处理数据
  while (const auto datagram = udp_channel_.receive()) {
    const auto packet = udp_codec::decodePacket(
        datagram->payload.data(), datagram->payload.size());

    // 忽略无效或非状态报文
    if (!packet || packet->body_case() != pb::UdpPacket::kState)
      continue;

    const pb::ChassisState &state = packet->state();

    // 保存合法状态并推送给 Web
    if (!state_cache_.update(state, packet->sequence(), datagram->source))
      continue;
    web_server_.sendText(web_protocol::serializeVehicleState(state));

    // 同步当前详情车辆的实际状态
    if (selected_vehicle_id_ && *selected_vehicle_id_ == state.vehicle_id()) {
      command_generator_.syncVehicleState(state);
    }
  }
}

// 处理一条 Web 页面消息
void Cockpit::processWebMessage(const std::string &message) {
  const web_protocol::Command command = web_protocol::parseCommand(message);
  switch (command.type) {
  case web_protocol::CommandType::SELECT_VEHICLE:
    selectVehicle(command.vehicle_id);
    break;
  case web_protocol::CommandType::DESELECT_VEHICLE:
    deselectVehicle();
    break;
  case web_protocol::CommandType::UNKNOWN:
    break;
  }
}

// 从 evdev 完整帧更新输入状态并生成控制指令
void Cockpit::receiveInputDevice() {
  // poll 发现 eventX 可读后，在这里取出完整帧并生成控制指令
  for (const InputDeviceState &input : input_device_reader_.readStates()) {
    if (selected_vehicle_id_)
      command_generator_.processInputState(input);
  }
}

// 记录详情车辆并初始化控制状态
void Cockpit::selectVehicle(const std::string &vehicle_id) {
  // 记录当前详情车辆并清除旧指令
  selected_vehicle_id_ = vehicle_id;
  command_generator_.reset();

  // 使用缓存的车辆状态初始化控制指令
  if (const auto *state_record = state_cache_.record(vehicle_id))
    command_generator_.syncVehicleState(state_record->state);
}

// 请求退出远程模式并清理当前选择
void Cockpit::deselectVehicle() {
  if (!selected_vehicle_id_)
    return;

  // 请求当前车辆退出远程模式
  pb::ControlCommand exit_command;
  exit_command.set_remote_mode_request(pb::REMOTE_MODE_REQUEST_EXIT);
  sendControlCommand(*selected_vehicle_id_, exit_command);

  // 清除详情车辆和控制状态
  selected_vehicle_id_.reset();
  command_generator_.reset();
  last_control_sent_ = {};
}

// 查找车辆端点并发送控制指令
bool Cockpit::sendControlCommand(const std::string &vehicle_id,
                                 pb::ControlCommand command) {
  const auto vehicle_address = state_cache_.vehicleAddress(vehicle_id);
  if (!vehicle_address)
    return false;

  command.set_cockpit_id(cockpit_id_);

  // 分配序号并编码控制指令
  const std::uint32_t sequence = next_control_sequence_++;
  const auto packet = udp_codec::encodeControlCommand(command, sequence);

  // 发送完整控制数据报
  if (!udp_channel_.send(*vehicle_address, packet.data(), packet.size())) {
    perror("send control");
    return false;
  }
  return true;
}

// 将车辆在线状态推送给 Web 页面
void Cockpit::publishVehicleList(Clock::time_point now) {
  auto vehicles = state_cache_.vehicleStatusList();
  web_server_.sendText(web_protocol::serializeVehicleStatusList(vehicles));
  last_vehicle_list_sent_ = now;
}
