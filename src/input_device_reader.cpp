#include "input_device_reader.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace input_device {
namespace {

constexpr std::int32_t kAutocenterStrength = 0xFFFF * 30 / 100;

// 将 errno 转为便于日志定位的错误文本
std::string systemError(const std::string &action) {
  return action + ": " + std::strerror(errno);
}

// 根据 Linux 按键码直接更新对应的语义状态
void setButton(InputDeviceState &state, std::uint16_t code, bool pressed) {
  switch (code) {
    case BTN_JOYSTICK + 0: state.cross_pressed = pressed; break;
    case BTN_JOYSTICK + 1: state.square_pressed = pressed; break;
    case BTN_JOYSTICK + 2: state.circle_pressed = pressed; break;
    case BTN_JOYSTICK + 3: state.triangle_pressed = pressed; break;
    case BTN_JOYSTICK + 4: state.r1_pressed = pressed; break;
    case BTN_JOYSTICK + 5: state.l1_pressed = pressed; break;
    case BTN_JOYSTICK + 6: state.r2_pressed = pressed; break;
    case BTN_JOYSTICK + 7: state.l2_pressed = pressed; break;
    case BTN_JOYSTICK + 8: state.select_pressed = pressed; break;
    case BTN_JOYSTICK + 9: state.start_pressed = pressed; break;
    case BTN_JOYSTICK + 10: state.r3_pressed = pressed; break;
    case BTN_JOYSTICK + 11: state.l3_pressed = pressed; break;
    case BTN_GAMEPAD + 3: state.plus_pressed = pressed; break;
    case BTN_GAMEPAD + 4: state.minus_pressed = pressed; break;
    case BTN_GAMEPAD + 5: state.encoder_clockwise_pressed = pressed; break;
    case BTN_GAMEPAD + 6:
      state.encoder_counter_clockwise_pressed = pressed;
      break;
    case BTN_GAMEPAD + 7: state.encoder_confirm_pressed = pressed; break;
    default: break;
  }
}

// 将方向盘原始轴线性缩放并钳制到 [-1.0, 1.0]，负值向左，正值向右
double normalizeSteering(std::int32_t value, AxisRange range) {
  if (range.minimum >= range.maximum) return 0.0;
  const double center =
      (static_cast<double>(range.minimum) + range.maximum) / 2.0;
  const double half_range =
      (static_cast<double>(range.maximum) - range.minimum) / 2.0;
  return std::clamp((static_cast<double>(value) - center) / half_range,
                    -1.0, 1.0);
}

// 将踏板原始轴线性缩放并钳制到 [0.0, 1.0]
double normalizePedal(std::int32_t value, AxisRange range) {
  if (range.minimum >= range.maximum) return 0.0;
  const double normalized =
      (static_cast<double>(value) - range.minimum) /
      (static_cast<double>(range.maximum) - range.minimum);
  return std::clamp(normalized, 0.0, 1.0);
}

// 将方向帽的 X/Y 轴方向直接转换为语义方向
PovDirection hatToDirection(std::int32_t x, std::int32_t y) {
  if (x == 0 && y < 0) return PovDirection::UP;
  if (x > 0 && y < 0) return PovDirection::UP_RIGHT;
  if (x > 0 && y == 0) return PovDirection::RIGHT;
  if (x > 0 && y > 0) return PovDirection::DOWN_RIGHT;
  if (x == 0 && y > 0) return PovDirection::DOWN;
  if (x < 0 && y > 0) return PovDirection::DOWN_LEFT;
  if (x < 0 && y == 0) return PovDirection::LEFT;
  if (x < 0 && y < 0) return PovDirection::UP_LEFT;
  return PovDirection::CENTER;
}

}  // namespace

// 创建并打开指定的输入设备
InputDeviceReader::InputDeviceReader(const std::string &path) {
  openDevice(path);
}

// 保存各控制轴的原始范围，供后续事件归一化使用
void InputEventProcessor::setAxisRange(std::uint16_t code, AxisRange range) {
  switch (code) {
    case ABS_X:
      steering_range_ = range;
      break;
    case ABS_Y:
      throttle_range_ = range;
      break;
    case ABS_Z:
      brake_range_ = range;
      break;
    case ABS_RZ:
      clutch_range_ = range;
      break;
    default:
      break;
  }
}

// 累积单个输入事件并在帧结束时输出完整状态
bool InputEventProcessor::consume(const input_event &event,
                                  InputDeviceState &completed_state) {
  // 用轴事件更新当前设备状态；未变化的轴继续保留原值
  if (event.type == EV_ABS) {
    switch (event.code) {
      case ABS_X:
        state_.wheel = normalizeSteering(event.value, steering_range_);
        break;
      case ABS_Y:
        state_.accelerator_pedal =
            normalizePedal(event.value, throttle_range_);
        break;
      case ABS_Z:
        state_.brake_pedal = normalizePedal(event.value, brake_range_);
        break;
      case ABS_RZ:
        state_.clutch_pedal = normalizePedal(event.value, clutch_range_);
        break;
      case ABS_HAT0X:
        hat_x_ = event.value;
        state_.pov = hatToDirection(hat_x_, hat_y_);
        break;
      case ABS_HAT0Y:
        hat_y_ = event.value;
        state_.pov = hatToDirection(hat_x_, hat_y_);
        break;
      default:
        break;
    }
  } else if (event.type == EV_KEY) {
    // 用按键事件更新当前设备状态；未变化的按键继续保留原值
    setButton(state_, event.code, event.value != 0);
  } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
    // SYN_REPORT 表示本批事件结束，输出此时的完整设备状态
    completed_state = state_;
    return true;
  }
  return false;
}

// 关闭持有的输入设备
InputDeviceReader::~InputDeviceReader() {
  if (fd_ >= 0) close(fd_);
}

// 打开 eventX，读取轴范围，并请求将自动回正强度设为 30%
bool InputDeviceReader::openDevice(const std::string &path) {
  path_ = path;

  // 关闭之前打开的设备
  if (fd_ >= 0) close(fd_);

  // 以非阻塞读写模式打开设备
  fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd_ < 0) {
    error_ = systemError("open " + path);
    return false;
  }

  // 查询、校验并缓存方向盘和三个踏板的轴范围
  if (!loadAxisRanges()) {
    close(fd_);
    fd_ = -1;
    return false;
  }

  // 向支持力反馈的设备请求设置 30% 自动回正强度
  input_event autocenter{};
  autocenter.type = EV_FF;
  autocenter.code = FF_AUTOCENTER;
  autocenter.value = kAutocenterStrength;
  ::write(fd_, &autocenter, sizeof(autocenter));

  error_.clear();
  return true;
}

// 查询并缓存 ABS_X、ABS_Y、ABS_Z 和 ABS_RZ 的原始范围
bool InputDeviceReader::loadAxisRanges() {
  for (const int code : {ABS_X, ABS_Y, ABS_Z, ABS_RZ}) {
    input_absinfo info{};
    if (ioctl(fd_, EVIOCGABS(code), &info) < 0) {
      error_ = systemError("query input axis " + std::to_string(code));
      return false;
    }
    if (info.minimum >= info.maximum) {
      error_ = "invalid input axis range for code " + std::to_string(code);
      return false;
    }
    event_processor_.setAxisRange(
        code, AxisRange{static_cast<std::int32_t>(info.minimum),
                        static_cast<std::int32_t>(info.maximum)});
  }
  return true;
}

// 读取当前所有可用事件，每遇到 SYN_REPORT 输出一帧完整状态
std::vector<InputDeviceState> InputDeviceReader::readAvailable() {
  std::vector<InputDeviceState> states;
  if (fd_ < 0) return states;

  // 批量读取当前可用的输入事件
  input_event events[64]{};
  while (true) {
    const ssize_t bytes = ::read(fd_, events, sizeof(events));
    if (bytes < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        error_ = systemError("read input event device");
      break;
    }
    if (bytes == 0) break;

    const auto count = static_cast<std::size_t>(bytes) / sizeof(input_event);
    // 逐个更新状态，并在 SYN_REPORT 到达时保存完整状态
    for (std::size_t index = 0; index < count; ++index) {
      InputDeviceState state{};
      if (event_processor_.consume(events[index], state)) {
        states.push_back(state);
      }
    }
    if (bytes < static_cast<ssize_t>(sizeof(events))) break;
  }
  return states;
}

}  // namespace input_device
