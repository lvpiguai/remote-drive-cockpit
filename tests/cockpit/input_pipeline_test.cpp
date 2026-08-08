#include "cockpit/control_command_generator.h"
#include "cockpit/input_device_state.h"

#include <cassert>
#include <chrono>
#include <cmath>

namespace {
bool near(double actual, double expected) {
  return std::abs(actual - expected) < 0.001;
}

bool is(SwitchCommand actual, SwitchCommand expected) {
  return actual == expected;
}

RemoteCtlCmd applyInput(ControlCommandGenerator &generator,
                        const InputDeviceState &input,
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
                .window_wiper,
            SwitchCommand::ON));
  assert(first_frame_generator.hasInput());
  first_frame_generator.reset();
  assert(!first_frame_generator.hasInput());

  InputDeviceState axes;
  axes.wheel = -1;
  axes.accelerator_pedal = 0.5;
  axes.brake_pedal = 1;
  const auto axis_command = applyInput(generator, axes, start);
  assert(near(axis_command.steering_angle, -90));
  assert(near(axis_command.acc_pedal, 50));
  assert(near(axis_command.brake_pedal, 100));
  assert(is(axis_command.light_brake, SwitchCommand::ON));
  assert(is(axis_command.parking, SwitchCommand::NO_CTL));

  // 换挡请求必须同时满足制动超过 20% 且车辆基本静止
  InputDeviceState drive;
  drive.l3_pressed = true;
  auto command = applyInput(generator, drive, start + 1ms);
  assert(command.gear == GearInfo::NEUTRAL);
  drive.brake_pedal = 0.22;
  command = applyInput(generator, drive, start + 2ms);
  assert(command.gear == GearInfo::DRIVE_1);
  drive.l3_pressed = false;
  generator.updateInput(drive, start + 3ms);
  assert(generator.generate(start + 3ms).gear == GearInfo::DRIVE_1);

  RemoteDrivingState moving{};
  moving.speed = 1.0;
  generator.syncVehicleState(moving);
  drive.select_pressed = true;
  command = applyInput(generator, drive, start + 4ms);
  assert(command.gear == GearInfo::DRIVE_1);
  moving.speed = 0.5;
  generator.syncVehicleState(moving);
  command = applyInput(generator, drive, start + 5ms);
  assert(command.gear == GearInfo::NEUTRAL);

  InputDeviceState wiper;
  generator.updateInput(wiper, start + 6ms);
  wiper.l2_pressed = true;
  assert(is(applyInput(generator, wiper, start + 7ms).window_wiper,
            SwitchCommand::ON));
  assert(is(applyInput(generator, wiper, start + 8ms).window_wiper,
            SwitchCommand::ON));
  wiper.l2_pressed = false;
  generator.updateInput(wiper, start + 9ms);
  wiper.l2_pressed = true;
  assert(is(applyInput(generator, wiper, start + 10ms).window_wiper,
            SwitchCommand::OFF));

  // Plus 和 Minus 分别控制举斗与降斗
  InputDeviceState bucket;
  bucket.plus_pressed = true;
  assert(applyInput(generator, bucket, start + 11ms).bucket_info ==
         BucketInfo::BUCKET_UP);
  bucket.plus_pressed = false;
  bucket.minus_pressed = true;
  assert(applyInput(generator, bucket, start + 12ms).bucket_info ==
         BucketInfo::BUCKET_DOWN);
  bucket.plus_pressed = true;
  assert(applyInput(generator, bucket, start + 13ms).bucket_info ==
         BucketInfo::BUCKET_KEEP);

  // Square 切换雾灯，Start 切换差速锁
  InputDeviceState switches;
  switches.square_pressed = true;
  assert(is(applyInput(generator, switches, start + 14ms).light_fog,
            SwitchCommand::ON));
  switches.square_pressed = false;
  generator.updateInput(switches, start + 15ms);
  switches.start_pressed = true;
  assert(is(applyInput(generator, switches, start + 16ms).diff_lock,
            SwitchCommand::ON));

  // 开关命令由实车状态确认后恢复 NO_CTL，外部状态变化不会被重复覆盖
  ControlCommandGenerator confirmation_generator;
  InputDeviceState confirmation_input;
  confirmation_generator.updateInput(confirmation_input, start + 17ms);
  confirmation_input.l2_pressed = true;
  assert(is(applyInput(confirmation_generator, confirmation_input,
                       start + 18ms)
                .window_wiper,
            SwitchCommand::ON));
  RemoteDrivingState confirmed_state{};
  confirmed_state.window_wiper = true;
  confirmation_generator.syncVehicleState(confirmed_state);
  assert(is(confirmation_generator.generate(start + 18ms).window_wiper,
            SwitchCommand::NO_CTL));
  confirmed_state.window_wiper = false;
  confirmation_generator.syncVehicleState(confirmed_state);
  assert(is(confirmation_generator.generate(start + 18ms).window_wiper,
            SwitchCommand::NO_CTL));
  confirmation_input.l2_pressed = false;
  confirmation_generator.updateInput(confirmation_input, start + 19ms);
  confirmation_input.l2_pressed = true;
  assert(is(applyInput(confirmation_generator, confirmation_input,
                       start + 20ms)
                .window_wiper,
            SwitchCommand::ON));
  confirmed_state.window_wiper = true;
  confirmation_generator.syncVehicleState(confirmed_state);
  confirmation_input.l2_pressed = false;
  confirmation_generator.updateInput(confirmation_input, start + 21ms);
  confirmation_input.l2_pressed = true;
  assert(is(applyInput(confirmation_generator, confirmation_input,
                       start + 22ms)
                .window_wiper,
            SwitchCommand::OFF));
  confirmed_state.window_wiper = false;
  confirmation_generator.syncVehicleState(confirmed_state);
  assert(is(confirmation_generator.generate(start + 22ms).window_wiper,
            SwitchCommand::NO_CTL));

  InputDeviceState parking;
  parking.clutch_pedal = 0.22;
  parking.l1_pressed = true;
  generator.updateInput(parking, start + 20ms);
  assert(is(generator.generate(start + 1019ms).parking,
            SwitchCommand::NO_CTL));
  assert(is(generator.generate(start + 1020ms).parking, SwitchCommand::OFF));
  assert(is(generator.generate(start + 2s).parking, SwitchCommand::OFF));

  InputDeviceState emergency;
  emergency.brake_pedal = 0.22;
  emergency.r1_pressed = true;
  generator.updateInput(emergency, start + 4s);
  assert(is(generator.generate(start + 4999ms).remote_emergency,
            SwitchCommand::NO_CTL));
  assert(is(generator.generate(start + 5s).remote_emergency,
            SwitchCommand::ON));

  InputDeviceState encoder;
  for (int i = 0; i < 12; ++i) {
    encoder.encoder_clockwise_pressed = true;
    generator.updateInput(encoder, start + 6s + i * 10ms);
    encoder.encoder_clockwise_pressed = false;
    generator.updateInput(encoder, start + 6s + i * 10ms + 1ms);
  }
  encoder.encoder_confirm_pressed = true;
  const auto enter_command = applyInput(generator, encoder, start + 6200ms);
  assert(enter_command.remoteMode == RemoteMode::REMOTE_ENTER);
  // 新的远控会话不得继承上一轮的驻车、急停、挡位或辅助开关状态
  assert(near(enter_command.steering_angle, 0));
  assert(near(enter_command.acc_pedal, 0));
  assert(near(enter_command.brake_pedal, 0));
  assert(enter_command.gear == GearInfo::NEUTRAL);
  assert(enter_command.bucket_info == BucketInfo::BUCKET_KEEP);
  assert(is(enter_command.parking, SwitchCommand::NO_CTL));
  assert(is(enter_command.remote_emergency, SwitchCommand::NO_CTL));
  assert(is(enter_command.light_fog, SwitchCommand::NO_CTL));
  assert(is(enter_command.diff_lock, SwitchCommand::NO_CTL));

  generator.reset();
  const auto reset_command = generator.generate(start + 7s);
  assert(reset_command.gear == GearInfo::NEUTRAL);
  assert(is(reset_command.parking, SwitchCommand::NO_CTL));
  assert(reset_command.remoteMode == RemoteMode::REMOTE_NO_CONTROL);
}
