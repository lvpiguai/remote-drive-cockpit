#include "cockpit_web_protocol.h"

#include <cstdio>
#include <sstream>

namespace cockpit_web {
namespace {

namespace pb = remote_drive::protocol;

// 转义 JSON 字符串
std::string jsonString(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    if (character == '\\' || character == '"')
      escaped.push_back('\\');
    escaped.push_back(character);
  }
  return escaped;
}

// 车辆驾驶模式文本
const char *modeName(pb::DriveMode mode) {
  switch (mode) {
  case pb::DRIVE_MODE_MANUAL:
    return "MANUAL";
  case pb::DRIVE_MODE_STANDBY:
    return "STANDBY";
  case pb::DRIVE_MODE_REMOTE:
    return "REMOTE";
  case pb::DRIVE_MODE_AUTO:
    return "AUTO";
  default:
    break;
  }
  return "UNKNOWN";
}

// 挡位文本
const char *gearName(pb::Gear gear) {
  switch (gear) {
  case pb::GEAR_NEUTRAL:
    return "N";
  case pb::GEAR_REVERSE_1:
    return "R1";
  case pb::GEAR_REVERSE_2:
    return "R2";
  case pb::GEAR_DRIVE_1:
    return "D1";
  case pb::GEAR_DRIVE_2:
    return "D2";
  case pb::GEAR_DRIVE_3:
    return "D3";
  default:
    break;
  }
  return "UNKNOWN";
}

// 远控指令文本
const char *remoteModeRequestName(pb::RemoteModeRequest mode) {
  switch (mode) {
  case pb::REMOTE_MODE_REQUEST_NONE:
    return "NONE";
  case pb::REMOTE_MODE_REQUEST_ENTER:
    return "ENTER";
  case pb::REMOTE_MODE_REQUEST_EXIT:
    return "EXIT";
  default:
    break;
  }
  return "UNKNOWN";
}

// 铲斗状态文本
const char *bucketName(pb::Bucket bucket) {
  switch (bucket) {
  case pb::BUCKET_UP:
    return "UP";
  case pb::BUCKET_DOWN:
    return "DOWN";
  case pb::BUCKET_KEEP:
    return "KEEP";
  default:
    break;
  }
  return "UNKNOWN";
}

// JSON 布尔字面量
const char *jsonBool(bool value) { return value ? "true" : "false"; }

// 三态开关文本
const char *switchCommandName(pb::SwitchCommand command) {
  switch (command) {
  case pb::SWITCH_NO_CONTROL:
    return "NO_CTL";
  case pb::SWITCH_OFF:
    return "OFF";
  case pb::SWITCH_ON:
    return "ON";
  default:
    break;
  }
  return "UNKNOWN";
}

} // namespace

// 解析 Web 选车命令
Command parseCommand(const std::string &message) {
  // 精确匹配无参数退车命令
  if (message == R"({"type":"deselect_vehicle"})") {
    return {CommandType::DESELECT_VEHICLE, {}};
  }

  char id[64]{};
  int consumed = 0;
  // 固定字段顺序并用 %n 拒绝尾随内容
  const int fields = std::sscanf(
      message.c_str(), R"({"type":"select_vehicle","vehicle_id":"%63[^"]"}%n)",
      id, &consumed);
  if (fields != 1 || consumed != static_cast<int>(message.size()) ||
      id[0] == 0) {
    return {};
  }
  return {CommandType::SELECT_VEHICLE, id};
}

// 序列化控制指令
std::string serializeControlCommand(const pb::RemoteDriveControlCommand &command,
                                    std::uint32_t sequence,
                                    const std::string &vehicle_id) {
  std::ostringstream json;
  json << R"({"type":"control","seq":)" << sequence << R"(,"vehicle_id":")"
       << jsonString(vehicle_id) << '"' << R"(,"cockpit_id":")"
       << jsonString(command.cockpit_id()) << '"' << R"(,"steering":)"
       << command.steering_angle() << R"(,"acc":)"
       << command.accelerator_percent() << R"(,"brake":)"
       << command.brake_percent() << R"(,"gear":")"
       << gearName(command.gear()) << R"(","bucket":")"
       << bucketName(command.bucket()) << R"(","remote":")"
       << remoteModeRequestName(command.remote_mode_request())
       << R"(","parking":")"
       << switchCommandName(command.parking()) << R"(","horn":")"
       << switchCommandName(command.horn()) << R"(","spray":")"
       << switchCommandName(command.spray()) << R"(","emergency":")"
       << switchCommandName(command.remote_emergency()) << R"(","wiper":")"
       << switchCommandName(command.window_wiper()) << R"(","brakeLight":")"
       << switchCommandName(command.light_brake()) << R"(","positionLight":")"
       << switchCommandName(command.light_position()) << R"(","lowBeam":")"
       << switchCommandName(command.light_near()) << R"(","highBeam":")"
       << switchCommandName(command.light_far()) << R"(","leftTurn":")"
       << switchCommandName(command.light_turn_left()) << R"(","rightTurn":")"
       << switchCommandName(command.light_turn_right())
       << R"(","rearWorkLight":")"
       << switchCommandName(command.light_working_rear())
       << R"(","warningLight":")" << switchCommandName(command.light_danger())
       << R"(","reverseLight":")" << switchCommandName(command.light_reverse())
       << R"(","hazardLight":")"
       << switchCommandName(command.light_double_flash())
       << R"(","frontLight":")" << switchCommandName(command.light_front())
       << R"(","sideWorkLight":")"
       << switchCommandName(command.light_working_side())
       << R"(","fogLight":")" << switchCommandName(command.light_fog())
       << R"(","diffLock":")" << switchCommandName(command.diff_lock())
       << R"("})";
  return json.str();
}

// 序列化车辆状态
std::string serializeVehicleState(const pb::ChassisState &state,
                                  std::uint32_t sequence) {
  std::ostringstream json;
  json << R"({"type":"state","seq":)" << sequence << R"(,"vehicle_id":")"
       << jsonString(state.vehicle_id()) << '"' << R"(,"controller_id":")"
       << jsonString(state.controller_id()) << '"' << R"(,"mode":")"
       << modeName(state.drive_mode()) << R"(","steering":)"
       << state.steering_angle() << R"(,"speed":)" << state.speed()
       << R"(,"gear":")" << gearName(state.gear()) << R"(","bucket":")"
       << bucketName(state.bucket()) << R"(","parking":)"
       << jsonBool(state.parking()) << R"(,"horn":)" << jsonBool(state.horn())
       << R"(,"spray":)" << jsonBool(state.spray()) << R"(,"emergency":)"
       << jsonBool(state.emergency()) << R"(,"wiper":)"
       << jsonBool(state.window_wiper()) << R"(,"brakeLight":)"
       << jsonBool(state.light_brake()) << R"(,"positionLight":)"
       << jsonBool(state.light_position()) << R"(,"lowBeam":)"
       << jsonBool(state.light_near()) << R"(,"highBeam":)"
       << jsonBool(state.light_far()) << R"(,"leftTurn":)"
       << jsonBool(state.light_turn_left()) << R"(,"rightTurn":)"
       << jsonBool(state.light_turn_right()) << R"(,"rearWorkLight":)"
       << jsonBool(state.light_working_rear()) << R"(,"warningLight":)"
       << jsonBool(state.light_danger()) << R"(,"reverseLight":)"
       << jsonBool(state.light_reverse()) << R"(,"hazardLight":)"
       << jsonBool(state.light_double_flash()) << R"(,"frontLight":)"
       << jsonBool(state.light_front()) << R"(,"sideWorkLight":)"
       << jsonBool(state.light_working_side()) << R"(,"fogLight":)"
       << jsonBool(state.light_fog()) << R"(,"diffLock":)"
       << jsonBool(state.diff_lock()) << '}';
  return json.str();
}

// 序列化车辆在线状态列表
std::string
serializeVehicleStatusList(const std::vector<VehicleOnlineStatus> &vehicles,
                           const std::string &selected_vehicle_id,
                           const std::string &cockpit_id) {
  std::ostringstream json;
  json << R"({"type":"vehicles","cockpit_id":")" << jsonString(cockpit_id)
       << R"(","selected":)";
  // 未选车时输出 null
  if (selected_vehicle_id.empty()) {
    json << "null";
  } else {
    json << '"' << jsonString(selected_vehicle_id) << '"';
  }
  json << R"(,"vehicles":[)";
  // 输出完整列表供前端覆盖旧状态
  bool first = true;
  for (const auto &vehicle : vehicles) {
    if (!first)
      json << ',';
    first = false;
    json << R"({"id":")" << jsonString(vehicle.id) << R"(","online":)"
         << jsonBool(vehicle.online) << R"(,"controller_id":")"
         << jsonString(vehicle.controller_id) << R"(","mode":")"
         << jsonString(vehicle.drive_mode) << R"("})";
  }
  json << "]}";
  return json.str();
}

} // namespace cockpit_web
