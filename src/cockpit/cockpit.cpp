#include "cockpit/cockpit.h"

#include <poll.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "cockpit/cockpit_web_protocol.h"
#include "protocol/udp_protocol.h"

namespace {

constexpr auto kVehicleOnlineTimeout = std::chrono::milliseconds(2500);
constexpr auto kVehicleListInterval = std::chrono::milliseconds(200);
constexpr auto kControlInterval = std::chrono::milliseconds(100);

// 将驾驶模式转换为日志文本
const char *modeName(DriveMode mode) {
  switch (mode) {
  case DriveMode::MANUAL:
    return "MANUAL";
  case DriveMode::STANDBY:
    return "STANDBY";
  case DriveMode::REMOTE:
    return "REMOTE";
  case DriveMode::AUTO:
    return "AUTO";
  }
  return "UNKNOWN";
}

// 将挡位转换为日志文本
const char *gearName(GearInfo gear) {
  switch (gear) {
  case GearInfo::NEUTRAL:
    return "N";
  case GearInfo::REVERSE_1:
    return "R1";
  case GearInfo::REVERSE_2:
    return "R2";
  case GearInfo::DRIVE_1:
    return "D1";
  case GearInfo::DRIVE_2:
    return "D2";
  case GearInfo::DRIVE_3:
    return "D3";
  }
  return "UNKNOWN";
}

} // namespace

// 创建驾驶舱并注册输入设备和可选车辆
Cockpit::Cockpit(std::string cockpit_id, std::string input_device_path,
                 std::vector<std::string> vehicle_ids,
                 std::uint16_t vehicle_udp_port, std::uint16_t websocket_port)
    : input_device_reader_(input_device_path),
      command_sender_(vehicle_channel_),
      heartbeat_cache_(std::move(vehicle_ids), kVehicleOnlineTimeout),
      cockpit_id_(std::move(cockpit_id)), vehicle_udp_port_(vehicle_udp_port),
      websocket_port_(websocket_port) {}

// 初始化资源并持续运行驾驶舱事件循环
int Cockpit::run() {
  // 初始化通信资源
  if (!initialize())
    return 1;

  std::cout << "驾驶舱 " << cockpit_id_
            << " 已启动，Web 控制页：ws://127.0.0.1:" << websocket_port_
            << "，输入设备：" << input_device_reader_.path()
            << "，等待车辆心跳\n";

  // 持续运行事件循环
  while (true) {
    // 组装监听端点
    pollfd descriptors[] = {
        {vehicle_channel_.fd(), POLLIN, 0},
        {web_server_.listenerFd(), POLLIN, 0},
        {web_server_.clientFd(), POLLIN, 0},
        {input_device_reader_.fd(), POLLIN, 0},
    };

    // 等待通信事件
    const int result = poll(descriptors, 4, 100);
    if (result < 0) {
      perror("cockpit poll");
      return 1;
    }

    // 处理就绪事件
    if (descriptors[0].revents & POLLIN)
      receiveVehiclePackets();
    if (descriptors[1].revents & POLLIN)
      web_server_.acceptClient();
    if (descriptors[2].revents & (POLLIN | POLLHUP | POLLERR))
      receiveWebMessages();
    if (descriptors[3].revents & POLLIN)
      receiveInputDevice();
    if (descriptors[3].revents & (POLLHUP | POLLERR | POLLNVAL)) {
      deselectVehicle("输入设备断开");
      std::cerr << "输入设备已断开，驾驶舱停止运行\n";
      return 1;
    }

    // 同步 Web 连接状态
    updateWebConnectionState();

    // 使用同一时间点处理本轮车辆状态和周期任务
    const auto now = Clock::now();
    updateControl(now);

    // 每 200ms 向 Web 推送完整车辆列表
    if (now - last_vehicle_list_sent_ >= kVehicleListInterval) {
      publishVehicleList(now);
    }
  }
}

// 初始化 WebSocket 监听和 UDP 通信端点
bool Cockpit::initialize() {
  // 输入设备由采集组件持有，不关心它来自真实或虚拟设备
  if (input_device_reader_.fd() < 0) {
    std::cerr << "打开输入设备失败：" << input_device_reader_.error() << '\n';
    return false;
  }

  // 创建并启动 Web 控制页监听端点
  if (!web_server_.startListening(websocket_port_))
    return false;

  // 创建车辆通信端点
  if (!vehicle_channel_.bindPort(vehicle_udp_port_)) {
    perror("cockpit socket");
    return false;
  }
  return true;
}

// 接收并分发车辆数据
void Cockpit::receiveVehiclePackets() {
  UdpDatagram datagram;
  while (vehicle_channel_.receive(datagram)) {
    const auto packet = remote_protocol::decodePacket(
        datagram.payload.data(), datagram.payload.size());
    if (!packet)
      continue;
    if (packet->body == remote_protocol::PacketBody::HEARTBEAT) {
      handleHeartbeat(packet->vehicle_id, packet->sequence, datagram.source);
    } else if (packet->body == remote_protocol::PacketBody::VEHICLE_STATE) {
      handleState(packet->state, packet->sequence, datagram.source);
    }
  }
}

// 更新车辆心跳
void Cockpit::handleHeartbeat(const std::string &vehicle_id,
                              std::uint32_t sequence,
                              const sockaddr_in &source) {
  // 仅接受指定车辆的新心跳，在线状态由周期快照统一推送
  heartbeat_cache_.updateHeartbeat(vehicle_id, source, sequence);
}

// 解码并更新对应车辆的最新状态
void Cockpit::handleState(const RemoteDrivingState &state,
                          std::uint32_t sequence,
                          const sockaddr_in &source) {
  // 校验协议、车辆归属和通信地址
  const std::string vehicle_id(state.vehicle_id);
  if (!heartbeat_cache_.matchesEndpoint(vehicle_id, source))
    return;

  const auto *previous = state_cache_.record(vehicle_id);

  // 状态关键量变化时输出终端事件
  const bool discrete_changed =
      !previous || state.remoteMode != previous->state.remoteMode ||
      state.gear != previous->state.gear ||
      state.parking != previous->state.parking ||
      state.emergency != previous->state.emergency ||
      std::strcmp(state.controller_id, previous->state.controller_id) != 0;
  if (discrete_changed) {
    std::cout << "[状态接收] vehicle=" << vehicle_id << " seq=" << sequence
              << " mode=" << modeName(state.remoteMode)
              << " speed=" << state.speed << " steering=" << state.steering
              << " gear=" << gearName(state.gear)
              << " parking=" << state.parking
              << " emergency=" << state.emergency
              << " controller=" << state.controller_id << '\n';
  }

  // 所有合法车辆状态都独立保存并推送，不依赖当前选择
  state_cache_.update(vehicle_id, state, sequence);
  web_server_.sendText(cockpit_web::serializeVehicleState(state, sequence));

  if (selected_vehicle_id_ && *selected_vehicle_id_ == vehicle_id) {
    const std::string controller_id(state.controller_id);
    if (!controller_id.empty() && controller_id != cockpit_id_) {
      clearVehicleSelection("车辆已由其他驾驶舱接管");
      return;
    }
    command_generator_.syncVehicleState(state);
  }
}

// 接收 Web 页面的车辆选择消息
void Cockpit::receiveWebMessages() {
  web_server_.receiveMessages([this](const std::string &message) {
    const cockpit_web::Command command = cockpit_web::parseCommand(message);
    switch (command.type) {
    case cockpit_web::CommandType::SELECT_VEHICLE:
      selectVehicle(command.vehicle_id);
      break;
    case cockpit_web::CommandType::DESELECT_VEHICLE:
      deselectVehicle();
      break;
    case cockpit_web::CommandType::UNKNOWN:
      break;
    }
  });
}

// 从 evdev 完整帧更新输入状态并生成控制指令
void Cockpit::receiveInputDevice() {
  // poll 发现 eventX 可读后，在这里取出完整帧并生成控制指令
  for (const InputDeviceState &input : input_device_reader_.readAvailable()) {
    if (selected_vehicle_id_)
      command_generator_.updateInput(input);
  }
}

// 验证车辆状态后记录当前选择
void Cockpit::selectVehicle(const std::string &vehicle_id) {
  if (selected_vehicle_id_ && *selected_vehicle_id_ == vehicle_id)
    return;
  if (!heartbeat_cache_.isFresh(vehicle_id))
    return;
  if (!state_cache_.isFresh(vehicle_id))
    return;

  const auto *state_record = state_cache_.record(vehicle_id);
  if (!state_record)
    return;
  const std::string controller_id(state_record->state.controller_id);
  if (!controller_id.empty() && controller_id != cockpit_id_)
    return;

  deselectVehicle("切换控制车辆");
  selected_vehicle_id_ = vehicle_id;
  command_generator_.reset();
  command_generator_.syncVehicleState(state_record->state);
  last_control_sent_ = Clock::now();
  std::cout << "已选择车辆 " << vehicle_id << '\n';
}

// 发送退出指令后清理当前选择和指令状态
void Cockpit::deselectVehicle(const char *reason) {
  if (!selected_vehicle_id_)
    return;

  const std::string vehicle_id = *selected_vehicle_id_;
  RemoteCtlCmd exit_command{};
  exit_command.remoteMode = RemoteMode::REMOTE_EXIT;
  sendControlCommand(vehicle_id, exit_command);

  clearVehicleSelection(reason);
  std::cout << "退出指令未送达时由车端断联保护兜底\n";
}

// 清理本地车辆选择和指令状态
void Cockpit::clearVehicleSelection(const char *reason) {
  if (!selected_vehicle_id_)
    return;

  const std::string vehicle_id = *selected_vehicle_id_;

  selected_vehicle_id_.reset();
  command_generator_.reset();
  last_control_sent_ = {};
  std::cout << reason << "，已停止控制车辆 " << vehicle_id << '\n';
}

// 检查所选车辆状态并按周期生成和发送控制指令
void Cockpit::updateControl(Clock::time_point now) {
  if (!selected_vehicle_id_)
    return;

  const std::string &vehicle_id = *selected_vehicle_id_;
  if (!heartbeat_cache_.isFresh(vehicle_id)) {
    deselectVehicle("车辆心跳超时");
    return;
  }
  if (!state_cache_.isFresh(vehicle_id)) {
    deselectVehicle("车辆状态回传超时");
    return;
  }

  if (!command_generator_.hasInput() ||
      now - last_control_sent_ < kControlInterval) {
    return;
  }

  const auto *state_record = state_cache_.record(vehicle_id);
  if (!state_record)
    return;
  const std::string controller_id(state_record->state.controller_id);
  if (!controller_id.empty() && controller_id != cockpit_id_) {
    clearVehicleSelection("车辆控制权已转移");
    return;
  }
  const RemoteCtlCmd command = command_generator_.generate(now);
  if (command.remoteMode != RemoteMode::REMOTE_ENTER &&
      (state_record->state.remoteMode != DriveMode::REMOTE ||
       controller_id != cockpit_id_)) {
    return;
  }

  if (sendControlCommand(vehicle_id, command))
    last_control_sent_ = now;
}

// 查找车辆端点并发送控制指令
bool Cockpit::sendControlCommand(const std::string &vehicle_id,
                                 RemoteCtlCmd command) {
  const auto endpoint = heartbeat_cache_.endpoint(vehicle_id);
  if (!endpoint)
    return false;

  std::memset(command.cockpit_id, 0, sizeof(command.cockpit_id));
  std::memcpy(command.cockpit_id, cockpit_id_.data(), cockpit_id_.size());

  const auto sequence = command_sender_.send(command, *endpoint);
  if (!sequence) {
    perror("send control");
    return false;
  }
  web_server_.sendText(
      cockpit_web::serializeControlCommand(command, *sequence, vehicle_id));
  return true;
}

// 处理输入连接变化及断开安全退出
void Cockpit::updateWebConnectionState() {
  // 忽略未变化的连接状态
  const bool connected = web_server_.connected();
  if (connected == web_connected_)
    return;
  web_connected_ = connected;
  if (connected) {
    std::cout << "Web 控制页已连接\n";
    return;
  }

  // 断开时请求车辆退出，车端超时保护继续兜底
  deselectVehicle("Web 控制页断开");
  std::cout << "Web 控制页已断开\n";
}

// 将车辆在线状态和当前选择推送给 Web 页面
void Cockpit::publishVehicleList(Clock::time_point now) {
  auto vehicles = heartbeat_cache_.vehicleStatusList();
  for (auto &vehicle : vehicles) {
    const auto *record = state_cache_.record(vehicle.id);
    if (record && state_cache_.isFresh(vehicle.id)) {
      vehicle.controller_id = record->state.controller_id;
      vehicle.drive_mode = modeName(record->state.remoteMode);
    }
  }
  web_server_.sendText(cockpit_web::serializeVehicleStatusList(
      vehicles, selected_vehicle_id_ ? *selected_vehicle_id_ : std::string{},
      cockpit_id_));
  last_vehicle_list_sent_ = now;
}
