#include "simulation/virtual_g29_report.h"

#include <cassert>
#include <string>

namespace {

const std::string kValidReport = R"({
  "pov": 8,
  "ps_pressed": true,
  "encoder_confirm_pressed": false,
  "encoder_counter_clockwise_pressed": true,
  "encoder_clockwise_pressed": false,
  "minus_pressed": true,
  "plus_pressed": false,
  "l3_pressed": true,
  "r3_pressed": false,
  "start_pressed": true,
  "select_pressed": false,
  "l2_pressed": true,
  "r2_pressed": false,
  "l1_pressed": true,
  "r1_pressed": false,
  "triangle_pressed": true,
  "circle_pressed": false,
  "square_pressed": true,
  "cross_pressed": false,
  "clutch_axis": 32767,
  "brake_axis": 16384,
  "throttle_axis": 1,
  "steering_axis": -32768
})";

std::string replaceOnce(std::string value, const std::string &from,
                        const std::string &to) {
  const std::size_t position = value.find(from);
  assert(position != std::string::npos);
  value.replace(position, from.size(), to);
  return value;
}

}  // namespace

int main() {
  VirtualG29Report report{};
  assert(parseVirtualG29Report(kValidReport, report));
  assert(report.steering_axis == -32768);
  assert(report.throttle_axis == 1);
  assert(report.brake_axis == 16384);
  assert(report.clutch_axis == 32767);
  assert(!report.cross_pressed);
  assert(report.square_pressed);
  assert(report.triangle_pressed);
  assert(report.l1_pressed);
  assert(report.l2_pressed);
  assert(report.start_pressed);
  assert(report.l3_pressed);
  assert(report.minus_pressed);
  assert(report.encoder_counter_clockwise_pressed);
  assert(report.ps_pressed);
  assert(report.pov == 8);

  assert(!parseVirtualG29Report(
      replaceOnce(kValidReport, "\"pov\": 8", "\"pov\": 9"), report));
  assert(!parseVirtualG29Report(
      replaceOnce(kValidReport, "\"ps_pressed\"", "\"unknown_button\""),
      report));
  assert(!parseVirtualG29Report(
      replaceOnce(kValidReport, "\"ps_pressed\": true", "\"ps_pressed\": 1"),
      report));
  assert(!parseVirtualG29Report(kValidReport + " trailing", report));
  return 0;
}
