#include "cockpit_web_protocol.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
  namespace pb = remote_drive::protocol;

  // 校验 Web 选车命令只接受约定的精确 JSON 格式
  const auto select = web_protocol::parseCommand(
      R"({"type":"select_vehicle","vehicle_id":"truck_01"})");
  assert(select.type == web_protocol::CommandType::SELECT_VEHICLE);
  assert(select.vehicle_id == "truck_01");

  const auto deselect =
      web_protocol::parseCommand(R"({"type":"deselect_vehicle"})");
  assert(deselect.type == web_protocol::CommandType::DESELECT_VEHICLE);
  assert(deselect.vehicle_id.empty());

  assert(
      web_protocol::parseCommand(R"({"type":"select_vehicle","vehicle_id":""})")
          .type == web_protocol::CommandType::UNKNOWN);
  assert(web_protocol::parseCommand(R"({"type":"unknown"})").type ==
         web_protocol::CommandType::UNKNOWN);

  // 车辆列表只携带车辆 ID 和在线状态
  const std::vector<VehicleOnlineStatus> vehicles = {
      {"truck_01", true},
      {"truck_\"02", false}};
  assert(
      web_protocol::serializeVehicleStatusList(vehicles) ==
      R"({"type":"vehicles","vehicles":[)"
      R"({"id":"truck_01","online":true},)"
      R"({"id":"truck_\"02","online":false}]})");
  assert(web_protocol::serializeVehicleStatusList({}) ==
         R"({"type":"vehicles","vehicles":[]})");

  // 状态快照完整展示车辆实际状态和当前控制驾驶舱
  pb::ChassisState state;
  state.set_vehicle_id("truck_01");
  state.set_controller_id("cockpit_02");
  state.set_drive_mode(pb::DRIVE_MODE_REMOTE);
  state.set_speed(4.5);
  state.set_gear(pb::GEAR_REVERSE_1);
  state.set_bucket(pb::BUCKET_DOWN);
  state.set_emergency(true);
  const std::string state_json = web_protocol::serializeVehicleState(state);
  assert(state_json.find(R"("type":"state","vehicle_id":"truck_01")") !=
         std::string::npos);
  assert(state_json.find(R"("controller_id":"cockpit_02")") !=
         std::string::npos);
  assert(state_json.find(R"("mode":"REMOTE")") != std::string::npos);
  assert(state_json.find(R"("speed":4.5,"gear":"R1","bucket":"DOWN")") !=
         std::string::npos);
  assert(state_json.find(R"("emergency":true)") != std::string::npos);
}
