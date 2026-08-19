#include "web_protocol.h"

#include <boost/json/src.hpp>

#include <string>
#include <utility>

namespace web_protocol {
namespace {

namespace pb = remote_drive::protocol;
namespace json = boost::json;

constexpr std::size_t kMaxVehicleIdLength = 63;

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

} // namespace

// 解析 Web 选车命令
std::optional<Command> parseCommand(const std::string &message) {
  boost::system::error_code error;
  const json::value parsed = json::parse(message, error);
  if (error || !parsed.is_object())
    return {};

  const json::object &object = parsed.as_object();
  const json::value *type = object.if_contains("type");
  if (!type || !type->is_string())
    return {};

  if (type->as_string() == "deselect_vehicle" && object.size() == 1) {
    return Command{CommandType::DESELECT_VEHICLE, {}};
  }

  if (type->as_string() != "select_vehicle" || object.size() != 2)
    return {};

  const json::value *vehicle_id = object.if_contains("vehicle_id");
  if (!vehicle_id || !vehicle_id->is_string() ||
      vehicle_id->as_string().empty() ||
      vehicle_id->as_string().size() > kMaxVehicleIdLength) {
    return {};
  }

  const json::string &id = vehicle_id->as_string();
  return Command{CommandType::SELECT_VEHICLE,
                 std::string(id.data(), id.size())};
}

// 序列化车辆状态
std::string serializeVehicleState(const pb::VehicleState &state) {
  json::object message;
  message["type"] = "state";
  message["vehicle_id"] = state.vehicle_id();
  message["controller_id"] = state.controller_id();
  message["mode"] = modeName(state.drive_mode());
  message["steering"] = state.steering_angle();
  message["speed"] = state.speed();
  message["gear"] = gearName(state.gear());
  message["bucket"] = bucketName(state.bucket());
  message["parking"] = state.parking();
  message["horn"] = state.horn();
  message["spray"] = state.spray();
  message["emergency"] = state.emergency();
  message["wiper"] = state.window_wiper();
  message["brakeLight"] = state.light_brake();
  message["positionLight"] = state.light_position();
  message["lowBeam"] = state.light_near();
  message["highBeam"] = state.light_far();
  message["leftTurn"] = state.light_turn_left();
  message["rightTurn"] = state.light_turn_right();
  message["rearWorkLight"] = state.light_working_rear();
  message["warningLight"] = state.light_danger();
  message["reverseLight"] = state.light_reverse();
  message["hazardLight"] = state.light_double_flash();
  message["frontLight"] = state.light_front();
  message["sideWorkLight"] = state.light_working_side();
  message["fogLight"] = state.light_fog();
  message["diffLock"] = state.diff_lock();
  return json::serialize(message);
}

// 序列化车辆在线状态列表
std::string
serializeVehicleStatusList(const std::vector<VehicleOnlineStatus> &vehicles) {
  json::array statuses;
  statuses.reserve(vehicles.size());

  // 输出完整列表供前端覆盖旧状态
  for (const auto &vehicle : vehicles) {
    json::object status;
    status["id"] = vehicle.id;
    status["online"] = vehicle.online;
    statuses.push_back(std::move(status));
  }

  json::object message;
  message["type"] = "vehicles";
  message["vehicles"] = std::move(statuses);
  return json::serialize(message);
}

} // namespace web_protocol
