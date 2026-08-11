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

pb::RemoteDriveControlCommand
applyInput(ControlCommandGenerator &generator, const InputDeviceState &input,
           ControlCommandGenerator::Clock::time_point now) {
  generator.updateInput(input, now);
  return generator.generate(now);
}

}  // namespace

int main() {
  using namespace std::chrono_literals;
  using Clock = ControlCommandGenerator::Clock;

  ControlCommandGenerator generator;
  const auto start = Clock::time_point{} + 10s;

  // 初始状态全松开，第一帧按下可直接产生上升沿
  ControlCommandGenerator first_frame_generator;
  assert(!first_frame_generator.hasInput());
  InputDeviceState first_frame_wiper;
  first_frame_wiper.l2_pressed = true;
  assert(is(applyInput(first_frame_generator, first_frame_wiper, start)
                .window_wiper(),
            pb::SWITCH_ON));
  assert(first_frame_generator.hasInput());
  first_frame_generator.reset();
  assert(!first_frame_generator.hasInput());

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
  assert(command.gear() == pb::GEAR_NEUTRAL);
  drive.brake_pedal = 0.22;
  command = applyInput(generator, drive, start + 2ms);
  assert(command.gear() == pb::GEAR_DRIVE_1);
  drive.l3_pressed = false;
  generator.updateInput(drive, start + 3ms);
  assert(generator.generate(start + 3ms).gear() == pb::GEAR_DRIVE_1);

  pb::ChassisState moving;
  moving.set_parking(true);
  moving.set_speed(1.0);
  generator.syncVehicleState(moving);
  drive.select_pressed = true;
  command = applyInput(generator, drive, start + 4ms);
  assert(command.gear() == pb::GEAR_DRIVE_1);
  moving.set_speed(0.5);
  generator.syncVehicleState(moving);
  command = applyInput(generator, drive, start + 5ms);
  assert(command.gear() == pb::GEAR_NEUTRAL);

  InputDeviceState wiper;
  generator.updateInput(wiper, start + 6ms);
  wiper.l2_pressed = true;
  assert(is(applyInput(generator, wiper, start + 7ms).window_wiper(),
            pb::SWITCH_ON));
  assert(is(applyInput(generator, wiper, start + 8ms).window_wiper(),
            pb::SWITCH_ON));
  wiper.l2_pressed = false;
  generator.updateInput(wiper, start + 9ms);
  wiper.l2_pressed = true;
  assert(is(applyInput(generator, wiper, start + 10ms).window_wiper(),
            pb::SWITCH_OFF));

  // Plus 和 Minus 分别控制举斗与降斗
  InputDeviceState bucket;
  bucket.plus_pressed = true;
  assert(applyInput(generator, bucket, start + 11ms).bucket() ==
         pb::BUCKET_UP);
  bucket.plus_pressed = false;
  bucket.minus_pressed = true;
  assert(applyInput(generator, bucket, start + 12ms).bucket() ==
         pb::BUCKET_DOWN);
  bucket.plus_pressed = true;
  assert(applyInput(generator, bucket, start + 13ms).bucket() ==
         pb::BUCKET_KEEP);

  // Square 切换雾灯，Start 切换差速锁
  InputDeviceState switches;
  switches.square_pressed = true;
  assert(is(applyInput(generator, switches, start + 14ms).light_fog(),
            pb::SWITCH_ON));
  switches.square_pressed = false;
  generator.updateInput(switches, start + 15ms);
  switches.start_pressed = true;
  assert(is(applyInput(generator, switches, start + 16ms).diff_lock(),
            pb::SWITCH_ON));

  // 开关命令由实车状态确认后恢复 NO_CTL，外部状态变化不会被重复覆盖
  ControlCommandGenerator confirmation_generator;
  InputDeviceState confirmation_input;
  confirmation_generator.updateInput(confirmation_input, start + 17ms);
  confirmation_input.l2_pressed = true;
  assert(is(applyInput(confirmation_generator, confirmation_input,
                       start + 18ms)
                .window_wiper(),
            pb::SWITCH_ON));
  pb::ChassisState confirmed_state;
  confirmed_state.set_window_wiper(true);
  confirmation_generator.syncVehicleState(confirmed_state);
  assert(is(confirmation_generator.generate(start + 18ms).window_wiper(),
            pb::SWITCH_NO_CONTROL));
  confirmed_state.set_window_wiper(false);
  confirmation_generator.syncVehicleState(confirmed_state);
  assert(is(confirmation_generator.generate(start + 18ms).window_wiper(),
            pb::SWITCH_NO_CONTROL));
  confirmation_input.l2_pressed = false;
  confirmation_generator.updateInput(confirmation_input, start + 19ms);
  confirmation_input.l2_pressed = true;
  assert(is(applyInput(confirmation_generator, confirmation_input,
                       start + 20ms)
                .window_wiper(),
            pb::SWITCH_ON));
  confirmed_state.set_window_wiper(true);
  confirmation_generator.syncVehicleState(confirmed_state);
  confirmation_input.l2_pressed = false;
  confirmation_generator.updateInput(confirmation_input, start + 21ms);
  confirmation_input.l2_pressed = true;
  assert(is(applyInput(confirmation_generator, confirmation_input,
                       start + 22ms)
                .window_wiper(),
            pb::SWITCH_OFF));
  confirmed_state.set_window_wiper(false);
  confirmation_generator.syncVehicleState(confirmed_state);
  assert(is(confirmation_generator.generate(start + 22ms).window_wiper(),
            pb::SWITCH_NO_CONTROL));

  InputDeviceState parking;
  parking.clutch_pedal = 0.22;
  parking.l1_pressed = true;
  generator.updateInput(parking, start + 20ms);
  assert(is(generator.generate(start + 1019ms).parking(),
            pb::SWITCH_NO_CONTROL));
  assert(is(generator.generate(start + 1020ms).parking(), pb::SWITCH_OFF));
  assert(is(generator.generate(start + 2s).parking(), pb::SWITCH_OFF));

  InputDeviceState emergency;
  emergency.brake_pedal = 0.22;
  emergency.r1_pressed = true;
  generator.updateInput(emergency, start + 4s);
  assert(is(generator.generate(start + 4999ms).remote_emergency(),
            pb::SWITCH_NO_CONTROL));
  assert(is(generator.generate(start + 5s).remote_emergency(),
            pb::SWITCH_ON));

  InputDeviceState encoder;
  for (int i = 0; i < 12; ++i) {
    encoder.encoder_clockwise_pressed = true;
    generator.updateInput(encoder, start + 6s + i * 10ms);
    encoder.encoder_clockwise_pressed = false;
    generator.updateInput(encoder, start + 6s + i * 10ms + 1ms);
  }
  encoder.encoder_confirm_pressed = true;
  const auto enter_command = applyInput(generator, encoder, start + 6200ms);
  assert(enter_command.remote_mode_request() == pb::REMOTE_MODE_REQUEST_ENTER);
  // 新的远控会话不得继承上一轮的驻车、急停、挡位或辅助开关状态
  assert(near(enter_command.steering_angle(), 0));
  assert(near(enter_command.accelerator_percent(), 0));
  assert(near(enter_command.brake_percent(), 0));
  assert(enter_command.gear() == pb::GEAR_NEUTRAL);
  assert(enter_command.bucket() == pb::BUCKET_KEEP);
  assert(is(enter_command.parking(), pb::SWITCH_NO_CONTROL));
  assert(is(enter_command.remote_emergency(), pb::SWITCH_NO_CONTROL));
  assert(is(enter_command.light_fog(), pb::SWITCH_NO_CONTROL));
  assert(is(enter_command.diff_lock(), pb::SWITCH_NO_CONTROL));

  generator.reset();
  const auto reset_command = generator.generate(start + 7s);
  assert(reset_command.gear() == pb::GEAR_NEUTRAL);
  assert(is(reset_command.parking(), pb::SWITCH_NO_CONTROL));
  assert(reset_command.remote_mode_request() == pb::REMOTE_MODE_REQUEST_NONE);
}
