#include "cockpit/cockpit_web_protocol.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

int main() {
  // 校验 Web 选车命令只接受约定的精确 JSON 格式
  const auto select = cockpit_web::parseCommand(
      R"({"type":"select_vehicle","vehicle_id":"truck_01"})");
  assert(select.type == cockpit_web::CommandType::SELECT_VEHICLE);
  assert(select.vehicle_id == "truck_01");

  const auto deselect =
      cockpit_web::parseCommand(R"({"type":"deselect_vehicle"})");
  assert(deselect.type == cockpit_web::CommandType::DESELECT_VEHICLE);
  assert(deselect.vehicle_id.empty());

  assert(
      cockpit_web::parseCommand(R"({"type":"select_vehicle","vehicle_id":""})")
          .type == cockpit_web::CommandType::UNKNOWN);
  assert(cockpit_web::parseCommand(R"({"type":"unknown"})").type ==
         cockpit_web::CommandType::UNKNOWN);

  // 车辆列表携带驾驶舱实例、控制权归属和实际驾驶模式
  const std::vector<VehicleOnlineStatus> vehicles = {
      {"truck_01", true, "cockpit_01", "REMOTE"},
      {"truck_\"02", false, "", ""}};
  assert(
      cockpit_web::serializeVehicleStatusList(vehicles, "truck_01",
                                              "cockpit_01") ==
      R"({"type":"vehicles","cockpit_id":"cockpit_01","selected":"truck_01","vehicles":[)"
      R"({"id":"truck_01","online":true,"controller_id":"cockpit_01","mode":"REMOTE"},)"
      R"({"id":"truck_\"02","online":false,"controller_id":"","mode":""}]})");
  assert(
      cockpit_web::serializeVehicleStatusList({}, "", "cockpit_02") ==
      R"({"type":"vehicles","cockpit_id":"cockpit_02","selected":null,"vehicles":[]})");

  // 控制快照完整展示目标车辆、驾驶舱和主要控制字段
  RemoteCtlCmd control{};
  std::memcpy(control.cockpit_id, "cockpit_01", 10);
  control.steering_angle = -12.5;
  control.acc_pedal = 20;
  control.brake_pedal = 30;
  control.gear = GearInfo::DRIVE_1;
  control.bucket_info = BucketInfo::BUCKET_UP;
  control.remoteMode = RemoteMode::REMOTE_ENTER;
  control.horn = SwitchCommand::ON;
  control.light_near = SwitchCommand::ON;
  const std::string control_json =
      cockpit_web::serializeControlCommand(control, 7, "truck_01");
  assert(control_json.find(R"("type":"control","seq":7)") != std::string::npos);
  assert(control_json.find(R"("cockpit_id":"cockpit_01")") !=
         std::string::npos);
  assert(control_json.find(R"("steering":-12.5,"acc":20,"brake":30)") !=
         std::string::npos);
  assert(control_json.find(R"("gear":"D1","bucket":"UP","remote":"ENTER")") !=
         std::string::npos);
  assert(control_json.find(R"("horn":"ON")") != std::string::npos);
  assert(control_json.find(R"("lowBeam":"ON")") != std::string::npos);
  assert(control_json.find(R"("parking":"NO_CTL")") != std::string::npos);

  // 状态快照完整展示车辆实际状态和当前控制驾驶舱
  RemoteDrivingState state{};
  std::memcpy(state.vehicle_id, "truck_01", 8);
  std::memcpy(state.controller_id, "cockpit_02", 10);
  state.remoteMode = DriveMode::REMOTE;
  state.speed = 4.5;
  state.gear = GearInfo::REVERSE_1;
  state.bucket = BucketInfo::BUCKET_DOWN;
  state.emergency = true;
  const std::string state_json = cockpit_web::serializeVehicleState(state, 9);
  assert(state_json.find(R"("type":"state","seq":9,"vehicle_id":"truck_01")") !=
         std::string::npos);
  assert(state_json.find(R"("controller_id":"cockpit_02")") !=
         std::string::npos);
  assert(state_json.find(R"("mode":"REMOTE")") != std::string::npos);
  assert(state_json.find(R"("speed":4.5,"gear":"R1","bucket":"DOWN")") !=
         std::string::npos);
  assert(state_json.find(R"("emergency":true)") != std::string::npos);
}
