#include "input_device_reader.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace {

input_event event(std::uint16_t type, std::uint16_t code, std::int32_t value) {
  input_event result{};
  result.type = type;
  result.code = code;
  result.value = value;
  return result;
}

bool near(double actual, double expected) {
  return std::abs(actual - expected) < 0.001;
}

InputDeviceState processFrame(
    input_device::InputEventProcessor &processor,
    std::initializer_list<input_event> events) {
  InputDeviceState state{};
  for (const input_event &input : events) {
    assert(!processor.consume(input, state));
  }
  assert(processor.consume(event(EV_SYN, SYN_REPORT, 0), state));
  return state;
}

}  // namespace

int main() {
  const input_device::AxisRange steering_range{0, 65534};
  const input_device::AxisRange pedal_range{100, 1100};
  input_device::InputEventProcessor processor;
  processor.setAxisRange(ABS_X, steering_range);
  processor.setAxisRange(ABS_Y, pedal_range);
  processor.setAxisRange(ABS_Z, pedal_range);
  processor.setAxisRange(ABS_RZ, pedal_range);

  // 通过事件处理器验证轴归一化和越界钳制
  InputDeviceState state =
      processFrame(processor, {event(EV_ABS, ABS_X, -1)});
  assert(near(state.wheel, -1));
  state = processFrame(processor, {event(EV_ABS, ABS_X, 32767)});
  assert(near(state.wheel, 0));
  state = processFrame(processor, {event(EV_ABS, ABS_X, 65535)});
  assert(near(state.wheel, 1));
  state = processFrame(
      processor, {event(EV_ABS, ABS_Y, 99), event(EV_ABS, ABS_Z, 600),
                  event(EV_ABS, ABS_RZ, 1101)});
  assert(near(state.accelerator_pedal, 0));
  assert(near(state.brake_pedal, 0.5));
  assert(near(state.clutch_pedal, 1));

  // 通过事件处理器验证方向帽的居中和八个方向
  struct HatCase {
    std::int32_t x;
    std::int32_t y;
    PovDirection expected;
  };
  const HatCase hat_cases[] = {
      {0, 0, PovDirection::CENTER},
      {0, -1, PovDirection::UP},
      {1, -1, PovDirection::UP_RIGHT},
      {1, 0, PovDirection::RIGHT},
      {1, 1, PovDirection::DOWN_RIGHT},
      {0, 1, PovDirection::DOWN},
      {-1, 1, PovDirection::DOWN_LEFT},
      {-1, 0, PovDirection::LEFT},
      {-1, -1, PovDirection::UP_LEFT},
  };
  for (const HatCase &hat : hat_cases) {
    state = processFrame(
        processor, {event(EV_ABS, ABS_HAT0X, hat.x),
                    event(EV_ABS, ABS_HAT0Y, hat.y)});
    assert(state.pov == hat.expected);
  }

  // 验证零散事件只在 SYN_REPORT 时组成完整帧
  assert(!processor.consume(event(EV_ABS, ABS_X, 32767), state));
  assert(!processor.consume(event(EV_ABS, ABS_Y, 1100), state));
  assert(!processor.consume(event(EV_ABS, ABS_Z, 600), state));
  assert(!processor.consume(event(EV_ABS, ABS_RZ, 100), state));
  assert(!processor.consume(event(EV_ABS, ABS_HAT0X, -1), state));
  assert(!processor.consume(event(EV_ABS, ABS_HAT0Y, -1), state));
  assert(!processor.consume(event(EV_KEY, BTN_JOYSTICK + 7, 1), state));
  assert(!processor.consume(event(EV_KEY, BTN_GAMEPAD + 3, 1), state));
  assert(!processor.consume(event(EV_KEY, BTN_GEAR_UP, 1), state));
  assert(processor.consume(event(EV_SYN, SYN_REPORT, 0), state));
  assert(near(state.wheel, 0));
  assert(near(state.accelerator_pedal, 1));
  assert(near(state.brake_pedal, 0.5));
  assert(near(state.clutch_pedal, 0));
  assert(state.l2_pressed);
  assert(state.plus_pressed);
  assert(state.pov == PovDirection::UP_LEFT);

  // 按键松开应直接清除语义状态，方向帽双轴回零后应恢复居中
  assert(!processor.consume(event(EV_KEY, BTN_JOYSTICK + 7, 0), state));
  assert(!processor.consume(event(EV_KEY, BTN_GAMEPAD + 3, 0), state));
  assert(!processor.consume(event(EV_ABS, ABS_HAT0X, 0), state));
  assert(!processor.consume(event(EV_ABS, ABS_HAT0Y, 0), state));
  assert(processor.consume(event(EV_SYN, SYN_REPORT, 0), state));
  assert(!state.l2_pressed);
  assert(!state.plus_pressed);
  assert(state.pov == PovDirection::CENTER);
}
