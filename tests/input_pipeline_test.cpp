#include "control_command_generator.h"
#include "input_device_state.h"

#include <cassert>
#include <chrono>
#include <cmath>

namespace {
namespace pb = remote_drive::protocol;

bool near(double actual, double expected) {
  return std::abs(actual - expected) < 0.001;
}

bool is(pb::SwitchCommand actual, pb::SwitchCommand expected) {
  return actual == expected;
}

bool is(pb::HoldCommand actual, pb::HoldCommand expected) {
  return actual == expected;
}

pb::ControlCommand
applyInput(ControlCommandGenerator &generator, const InputDeviceState &input,
           ControlCommandGenerator::Clock::time_point now) {
  generator.processInputState(input, now);
  return generator.generateCommand(now);
}

}  // namespace

int main() {
  using namespace std::chrono_literals;
  using Clock = ControlCommandGenerator::Clock;

  ControlCommandGenerator generator;
  const auto start = Clock::time_point{} + 10s;

  // 初始状态全松开，第一帧按下可直接产生上升沿
  ControlCommandGenerator first_frame_generator;
  InputDeviceState first_frame_wiper;
  first_frame_wiper.l2_pressed = true;
  assert(is(applyInput(first_frame_generator, first_frame_wiper, start)
                .window_wiper(),
            pb::SWITCH_ON));
  first_frame_generator.reset();

  InputDeviceState axes;
  axes.wheel = -1;
  axes.accelerator_pedal = 0.5;
  axes.brake_pedal = 1;
  const auto axis_command = applyInput(generator, axes, start);
  assert(near(axis_command.steering_angle(), -90));
  assert(near(axis_command.accelerator_percent(), 50));
  assert(near(axis_command.brake_percent(), 100));
  assert(is(axis_command.light_brake(), pb::SWITCH_ON));
  assert(is(axis_command.parking(), pb::SWITCH_NO_CONTROL));

  // 换挡请求必须同时满足制动超过 20% 且车辆基本静止
  InputDeviceState drive;
  drive.l3_pressed = true;
  auto command = applyInput(generator, drive, start + 1ms);
  assert(command.gear() == pb::GEAR_COMMAND_NO_CONTROL);
  drive.brake_pedal = 0.22;
  command = applyInput(generator, drive, start + 2ms);
  assert(command.gear() == pb::GEAR_COMMAND_DRIVE);
  drive.l3_pressed = false;
  generator.processInputState(drive, start + 3ms);
  assert(generator.generateCommand(start + 3ms).gear() ==
         pb::GEAR_COMMAND_DRIVE);

  pb::VehicleState moving;
  moving.set_parking(true);
  moving.set_speed(1.0);
  generator.syncVehicleState(moving);
  drive.select_pressed = true;
  command = applyInput(generator, drive, start + 4ms);
  assert(command.gear() == pb::GEAR_COMMAND_DRIVE);
  moving.set_speed(0.5);
  generator.syncVehicleState(moving);
  command = applyInput(generator, drive, start + 5ms);
  assert(command.gear() == pb::GEAR_COMMAND_NEUTRAL);
  generator.syncVehicleState(moving);
  assert(generator.generateCommand(start + 5ms).gear() ==
         pb::GEAR_COMMAND_NO_CONTROL);

  InputDeviceState wiper;
  generator.processInputState(wiper, start + 6ms);
  wiper.l2_pressed = true;
  assert(is(applyInput(generator, wiper, start + 7ms).window_wiper(),
            pb::SWITCH_ON));
  assert(is(applyInput(generator, wiper, start + 8ms).window_wiper(),
            pb::SWITCH_ON));
  wiper.l2_pressed = false;
  generator.processInputState(wiper, start + 9ms);
  wiper.l2_pressed = true;
  assert(is(applyInput(generator, wiper, start + 10ms).window_wiper(),
            pb::SWITCH_OFF));

  // Plus 和 Minus 分别控制举斗与降斗
  InputDeviceState bucket;
  bucket.plus_pressed = true;
  assert(applyInput(generator, bucket, start + 11ms).bucket() ==
         pb::BUCKET_COMMAND_UP);
  bucket.plus_pressed = false;
  bucket.minus_pressed = true;
  assert(applyInput(generator, bucket, start + 12ms).bucket() ==
         pb::BUCKET_COMMAND_DOWN);
  bucket.plus_pressed = true;
  assert(applyInput(generator, bucket, start + 13ms).bucket() ==
         pb::BUCKET_COMMAND_NO_CONTROL);
  bucket.plus_pressed = false;
  bucket.minus_pressed = false;
  assert(applyInput(generator, bucket, start + 14ms).bucket() ==
         pb::BUCKET_COMMAND_NO_CONTROL);

  // Square 切换雾灯，Start 切换差速锁
  InputDeviceState switches;
  switches.square_pressed = true;
  assert(is(applyInput(generator, switches, start + 15ms).light_fog(),
            pb::SWITCH_ON));
  switches.square_pressed = false;
  generator.processInputState(switches, start + 16ms);
  switches.start_pressed = true;
  assert(is(applyInput(generator, switches, start + 17ms).diff_lock(),
            pb::SWITCH_ON));

  // 开关命令由实车状态确认后恢复 NO_CTL，外部状态变化不会被重复覆盖
  ControlCommandGenerator confirmation_generator;
  InputDeviceState confirmation_input;
  confirmation_generator.processInputState(confirmation_input, start + 17ms);
  confirmation_input.l2_pressed = true;
  assert(is(applyInput(confirmation_generator, confirmation_input,
                       start + 18ms)
                .window_wiper(),
            pb::SWITCH_ON));
  pb::VehicleState confirmed_state;
  confirmed_state.set_window_wiper(true);
  confirmation_generator.syncVehicleState(confirmed_state);
  assert(is(confirmation_generator.generateCommand(start + 18ms).window_wiper(),
            pb::SWITCH_NO_CONTROL));
  confirmed_state.set_window_wiper(false);
  confirmation_generator.syncVehicleState(confirmed_state);
  assert(is(confirmation_generator.generateCommand(start + 18ms).window_wiper(),
            pb::SWITCH_NO_CONTROL));
  confirmation_input.l2_pressed = false;
  confirmation_generator.processInputState(confirmation_input, start + 19ms);
  confirmation_input.l2_pressed = true;
  assert(is(applyInput(confirmation_generator, confirmation_input,
                       start + 20ms)
                .window_wiper(),
            pb::SWITCH_ON));
  confirmed_state.set_window_wiper(true);
  confirmation_generator.syncVehicleState(confirmed_state);
  confirmation_input.l2_pressed = false;
  confirmation_generator.processInputState(confirmation_input, start + 21ms);
  confirmation_input.l2_pressed = true;
  assert(is(applyInput(confirmation_generator, confirmation_input,
                       start + 22ms)
                .window_wiper(),
            pb::SWITCH_OFF));
  confirmed_state.set_window_wiper(false);
  confirmation_generator.syncVehicleState(confirmed_state);
  assert(is(confirmation_generator.generateCommand(start + 22ms).window_wiper(),
            pb::SWITCH_NO_CONTROL));

  // 持续型输入被车辆确认后仍应保留，直到收到新的输入状态
  ControlCommandGenerator continuous_generator;
  InputDeviceState continuous_input;
  continuous_input.brake_pedal = 0.5;
  continuous_input.r2_pressed = true;
  continuous_input.pov = PovDirection::UP;
  continuous_generator.processInputState(continuous_input, start + 23ms);
  pb::VehicleState continuous_state;
  continuous_state.set_horn(true);
  continuous_state.set_spray(true);
  continuous_state.set_light_brake(true);
  continuous_generator.syncVehicleState(continuous_state);
  const auto continuous_command =
      continuous_generator.generateCommand(start + 24ms);
  assert(is(continuous_command.horn(), pb::HOLD_COMMAND_ON));
  assert(is(continuous_command.spray(), pb::HOLD_COMMAND_ON));
  assert(is(continuous_command.light_brake(), pb::SWITCH_ON));

  // 松开按住型控制后放弃控制权，而不是主动发送 OFF
  continuous_input.r2_pressed = false;
  continuous_input.pov = PovDirection::CENTER;
  const auto released_command =
      applyInput(continuous_generator, continuous_input, start + 25ms);
  assert(is(released_command.horn(), pb::HOLD_COMMAND_NO_CONTROL));
  assert(is(released_command.spray(), pb::HOLD_COMMAND_NO_CONTROL));

  InputDeviceState parking;
  parking.clutch_pedal = 0.22;
  parking.l1_pressed = true;
  generator.processInputState(parking, start + 20ms);
  parking.wheel = 0.5;
  generator.processInputState(parking, start + 300ms);
  assert(is(generator.generateCommand(start + 519ms).parking(),
            pb::SWITCH_NO_CONTROL));
  assert(is(generator.generateCommand(start + 520ms).parking(),
            pb::SWITCH_OFF));
  assert(is(generator.generateCommand(start + 2s).parking(),
            pb::SWITCH_OFF));

  InputDeviceState emergency;
  emergency.brake_pedal = 0.22;
  emergency.r1_pressed = true;
  generator.processInputState(emergency, start + 4s);
  assert(is(generator.generateCommand(start + 4499ms).remote_emergency(),
            pb::SWITCH_NO_CONTROL));
  assert(is(generator.generateCommand(start + 4500ms).remote_emergency(),
            pb::SWITCH_ON));

  InputDeviceState encoder;
  for (int i = 0; i < 12; ++i) {
    encoder.encoder_clockwise_pressed = true;
    generator.processInputState(encoder, start + 6s + i * 10ms);
    encoder.encoder_clockwise_pressed = false;
    generator.processInputState(encoder, start + 6s + i * 10ms + 1ms);
  }
  encoder.encoder_confirm_pressed = true;
  const auto enter_command = applyInput(generator, encoder, start + 6200ms);
  assert(enter_command.remote_mode_request() == pb::REMOTE_MODE_REQUEST_ENTER);
  // 新的远控会话不得继承上一轮的驻车、急停、挡位或辅助开关状态
  assert(near(enter_command.steering_angle(), 0));
  assert(near(enter_command.accelerator_percent(), 0));
  assert(near(enter_command.brake_percent(), 0));
  assert(enter_command.gear() == pb::GEAR_COMMAND_NO_CONTROL);
  assert(enter_command.bucket() == pb::BUCKET_COMMAND_NO_CONTROL);
  assert(is(enter_command.parking(), pb::SWITCH_NO_CONTROL));
  assert(is(enter_command.remote_emergency(), pb::SWITCH_NO_CONTROL));
  assert(is(enter_command.light_fog(), pb::SWITCH_NO_CONTROL));
  assert(is(enter_command.diff_lock(), pb::SWITCH_NO_CONTROL));

  const auto exit_command = generator.generateExitCommand();
  assert(exit_command.remote_mode_request() == pb::REMOTE_MODE_REQUEST_EXIT);
  assert(exit_command.gear() == pb::GEAR_COMMAND_NO_CONTROL);
  assert(exit_command.bucket() == pb::BUCKET_COMMAND_NO_CONTROL);

  generator.reset();
  const auto reset_command = generator.generateCommand(start + 7s);
  assert(reset_command.gear() == pb::GEAR_COMMAND_NO_CONTROL);
  assert(is(reset_command.parking(), pb::SWITCH_NO_CONTROL));
  assert(reset_command.remote_mode_request() == pb::REMOTE_MODE_REQUEST_NONE);
}
