#include "udp_codec.h"

#include <cassert>
#include <limits>

namespace {

namespace pb = remote_drive::protocol;

bool decodes(const pb::ChassisState &state) {
  const auto bytes = udp_codec::encodeDrivingState(state, 7);
  return udp_codec::decodePacket(bytes.data(), bytes.size()).has_value();
}

} // namespace

int main() {
  pb::ChassisState state;
  state.set_vehicle_id("truck_01");
  state.set_drive_mode(pb::DRIVE_MODE_STANDBY);
  assert(decodes(state));

  state.clear_vehicle_id();
  assert(!decodes(state));

  state.set_vehicle_id("truck_01");
  state.set_speed(std::numeric_limits<double>::infinity());
  assert(!decodes(state));

  state.set_speed(0);
  state.set_drive_mode(static_cast<pb::DriveMode>(99));
  assert(!decodes(state));
}
