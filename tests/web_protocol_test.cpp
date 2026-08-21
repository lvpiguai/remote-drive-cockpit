#include "web_protocol.h"

#include <boost/json.hpp>

#include <cassert>
#include <string>
#include <vector>

int main() {
  namespace pb = remote_drive::protocol;
  namespace json = boost::json;

  // 校验 Web 选车命令接受标准 JSON 的空格和字段顺序变化
  const auto select = web_protocol::parseCommand(
      R"({ "vehicle_id": "truck_01", "type": "select_vehicle" })");
  assert(select);
  assert(select->type == web_protocol::CommandType::SELECT_VEHICLE);
  assert(select->vehicle_id == "truck_01");

  const auto deselect =
      web_protocol::parseCommand(R"({"type":"deselect_vehicle"})");
  assert(deselect);
  assert(deselect->type == web_protocol::CommandType::DESELECT_VEHICLE);
  assert(deselect->vehicle_id.empty());

  assert(!web_protocol::parseCommand(
      R"({"type":"select_vehicle","vehicle_id":""})"));
  assert(!web_protocol::parseCommand(R"({"type":"select_vehicle"})"));
  assert(!web_protocol::parseCommand(
      R"({"type":"select_vehicle","vehicle_id":1})"));
  assert(!web_protocol::parseCommand("not json"));
  assert(!web_protocol::parseCommand(R"({"type":"unknown"})"));

  // 车辆列表只携带车辆 ID 和在线状态
  const std::vector<VehicleOnlineStatus> vehicles = {
      {"truck_01", true},
      {"truck_\n\"02", false}};
  const json::object vehicle_list =
      json::parse(web_protocol::serializeVehicleStatusList(vehicles))
          .as_object();
  assert(vehicle_list.at("type").as_string() == "vehicles");
  const json::array &statuses = vehicle_list.at("vehicles").as_array();
  assert(statuses.size() == 2);
  assert(statuses[0].as_object().at("id").as_string() == "truck_01");
  assert(statuses[0].as_object().at("online").as_bool());
  assert(statuses[1].as_object().at("id").as_string() == "truck_\n\"02");
  assert(!statuses[1].as_object().at("online").as_bool());

  const json::object empty_list =
      json::parse(web_protocol::serializeVehicleStatusList({})).as_object();
  assert(empty_list.at("vehicles").as_array().empty());

  // 状态快照完整展示车辆实际状态和当前控制驾驶舱
  pb::VehicleState state;
  state.set_vehicle_id("truck_01");
  state.set_cockpit_id("cockpit_02");
  state.set_drive_mode(pb::DRIVE_MODE_REMOTE);
  state.set_speed(4.5);
  state.set_gear(pb::GEAR_STATE_REVERSE);
  state.set_bucket(pb::BUCKET_STATE_DOWN);
  state.set_emergency(true);
  const json::object state_message =
      json::parse(web_protocol::serializeVehicleState(state)).as_object();
  assert(state_message.at("type").as_string() == "state");
  assert(state_message.at("vehicle_id").as_string() == "truck_01");
  assert(state_message.at("cockpit_id").as_string() == "cockpit_02");
  assert(state_message.at("mode").as_string() == "REMOTE");
  assert(state_message.at("speed").as_double() == 4.5);
  assert(state_message.at("gear").as_string() == "R");
  assert(state_message.at("bucket").as_string() == "DOWN");
  assert(state_message.at("emergency").as_bool());
}
