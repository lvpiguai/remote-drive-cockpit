#include "udp_codec.h"

#include <cassert>
#include <limits>
#include <string>

namespace {

namespace pb = remote_drive::protocol;
constexpr std::uint32_t kMagic = 0x52445550;

udp_codec::PacketBytes statePacketBytes(const pb::ChassisState &state,
                                        std::uint32_t sequence) {
  pb::UdpPacket packet;
  packet.set_magic(kMagic);
  packet.set_sequence(sequence);
  packet.mutable_state()->CopyFrom(state);

  std::string bytes;
  assert(packet.SerializeToString(&bytes));
  return {bytes.begin(), bytes.end()};
}

bool decodesState(const pb::ChassisState &state) {
  const auto bytes = statePacketBytes(state, 7);
  return udp_codec::decodePacket(bytes.data(), bytes.size()).has_value();
}

void testControlEncoding() {
  pb::ControlCommand command;
  command.set_cockpit_id("cockpit_01");
  command.set_steering_angle(-12.5);
  command.set_accelerator_percent(35);
  command.set_brake_percent(2);
  command.set_gear(pb::GEAR_DRIVE_1);

  const auto bytes = udp_codec::encodeControlCommand(command, 42);
  assert(!bytes.empty());

  pb::UdpPacket packet;
  assert(packet.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())));
  assert(packet.magic() == kMagic);
  assert(packet.sequence() == 42);
  assert(packet.body_case() == pb::UdpPacket::kControl);
  assert(packet.control().cockpit_id() == "cockpit_01");
  assert(packet.control().steering_angle() == -12.5);
  assert(udp_codec::decodePacket(bytes.data(), bytes.size()));

  command.clear_cockpit_id();
  assert(udp_codec::encodeControlCommand(command, 43).empty());

  command.set_cockpit_id("cockpit_01");
  command.set_steering_angle(std::numeric_limits<double>::quiet_NaN());
  assert(udp_codec::encodeControlCommand(command, 44).empty());
}

void testStateDecoding() {
  pb::ChassisState state;
  state.set_vehicle_id("truck_01");
  state.set_drive_mode(pb::DRIVE_MODE_STANDBY);
  assert(decodesState(state));

  state.clear_vehicle_id();
  assert(!decodesState(state));

  state.set_vehicle_id("truck_01");
  state.set_speed(std::numeric_limits<double>::infinity());
  assert(!decodesState(state));

  state.set_speed(0);
  state.set_drive_mode(static_cast<pb::DriveMode>(99));
  assert(!decodesState(state));
}

} // namespace

int main() {
  testControlEncoding();
  testStateDecoding();
}
