#include "simulation/input_device_simulator.h"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

namespace {

// 虚拟设备身份和 Web 原始数据范围
constexpr char kDeviceName[] = "Remote Drive Virtual Input Device";
constexpr std::int32_t kSteeringMin = -32768;
constexpr std::int32_t kAxisMax = 32767;

struct ButtonMapping {
  std::uint16_t code;
  bool VirtualG29Report::*pressed;
};

constexpr ButtonMapping kButtonMappings[] = {
    {BTN_JOYSTICK + 0, &VirtualG29Report::cross_pressed},
    {BTN_JOYSTICK + 1, &VirtualG29Report::square_pressed},
    {BTN_JOYSTICK + 2, &VirtualG29Report::circle_pressed},
    {BTN_JOYSTICK + 3, &VirtualG29Report::triangle_pressed},
    {BTN_JOYSTICK + 4, &VirtualG29Report::r1_pressed},
    {BTN_JOYSTICK + 5, &VirtualG29Report::l1_pressed},
    {BTN_JOYSTICK + 6, &VirtualG29Report::r2_pressed},
    {BTN_JOYSTICK + 7, &VirtualG29Report::l2_pressed},
    {BTN_JOYSTICK + 8, &VirtualG29Report::select_pressed},
    {BTN_JOYSTICK + 9, &VirtualG29Report::start_pressed},
    {BTN_JOYSTICK + 10, &VirtualG29Report::r3_pressed},
    {BTN_JOYSTICK + 11, &VirtualG29Report::l3_pressed},
    {BTN_GAMEPAD + 3, &VirtualG29Report::plus_pressed},
    {BTN_GAMEPAD + 4, &VirtualG29Report::minus_pressed},
    {BTN_GAMEPAD + 5, &VirtualG29Report::encoder_clockwise_pressed},
    {BTN_GAMEPAD + 6, &VirtualG29Report::encoder_counter_clockwise_pressed},
    {BTN_GAMEPAD + 7, &VirtualG29Report::encoder_confirm_pressed},
    {BTN_GAMEPAD + 8, &VirtualG29Report::ps_pressed},
};

// 将 errno 转为便于日志定位的错误文本
std::string systemError(const std::string &action) {
  return action + ": " + std::strerror(errno);
}

// 声明虚拟设备支持的事件类型或事件码
bool setEventBit(int fd, unsigned long request, int code, std::string &error) {
  if (ioctl(fd, request, code) >= 0) return true;
  error = systemError("uinput ioctl");
  return false;
}

// 将统一的 POV 0～8 转为 Linux 方向帽坐标
void povToHat(std::uint8_t pov, std::int32_t &x, std::int32_t &y) {
  x = 0;
  y = 0;
  switch (pov) {
    case 1:
      y = -1;
      break;
    case 2:
      x = 1;
      y = -1;
      break;
    case 3:
      x = 1;
      break;
    case 4:
      x = 1;
      y = 1;
      break;
    case 5:
      y = 1;
      break;
    case 6:
      x = -1;
      y = 1;
      break;
    case 7:
      x = -1;
      break;
    case 8:
      x = -1;
      y = -1;
      break;
    default:
      break;
  }
}

}  // namespace

// 退出时由 RAII 自动销毁虚拟设备
InputDeviceSimulator::~InputDeviceSimulator() { stop(); }

// 创建 uinput 设备，并等待内核生成 eventX
bool InputDeviceSimulator::start() {
  stop();
  error_.clear();

  // uinput 是模拟器向 Linux 输入子系统写事件的入口
  fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd_ < 0) {
    error_ = systemError("open /dev/uinput");
    return false;
  }

  // 声明虚拟设备支持轴事件和按键事件
  if (!setEventBit(fd_, UI_SET_EVBIT, EV_ABS, error_) ||
      !setEventBit(fd_, UI_SET_EVBIT, EV_KEY, error_)) {
    stop();
    return false;
  }
  for (const int code : {ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y}) {
    if (!setEventBit(fd_, UI_SET_ABSBIT, code, error_)) {
      stop();
      return false;
    }
  }
  for (const ButtonMapping &button : kButtonMappings) {
    if (!setEventBit(fd_, UI_SET_KEYBIT, button.code, error_)) {
      stop();
      return false;
    }
  }

  // 设置设备名称、ID 和各轴范围
  uinput_user_dev device{};
  std::strncpy(device.name, kDeviceName, UINPUT_MAX_NAME_SIZE - 1);
  device.id.bustype = BUS_USB;
  device.id.vendor = 0x046d;
  device.id.product = 0xc24f;
  device.id.version = 1;
  device.absmin[ABS_X] = kSteeringMin;
  device.absmax[ABS_X] = kAxisMax;
  for (const int code : {ABS_Y, ABS_Z, ABS_RZ}) {
    device.absmin[code] = 0;
    device.absmax[code] = kAxisMax;
  }
  for (const int code : {ABS_HAT0X, ABS_HAT0Y}) {
    device.absmin[code] = -1;
    device.absmax[code] = 1;
  }
  if (::write(fd_, &device, sizeof(device)) !=
      static_cast<ssize_t>(sizeof(device))) {
    error_ = systemError("configure virtual input device");
    stop();
    return false;
  }
  if (ioctl(fd_, UI_DEV_CREATE) < 0) {
    error_ = systemError("create virtual input device");
    stop();
    return false;
  }

  // 内核创建完成后，找到它对应的 /dev/input/eventX
  char sysname[64]{};
  if (ioctl(fd_, UI_GET_SYSNAME(sizeof(sysname)), sysname) < 0) {
    error_ = systemError("query virtual input device sysname");
    stop();
    return false;
  }
  if (!findEventPath(sysname)) {
    stop();
    return false;
  }
  return true;
}

// udev 创建设备节点存在短暂延迟，因此最多等待一秒
bool InputDeviceSimulator::findEventPath(const std::string &sysname) {
  const auto sys_path = std::filesystem::path("/sys/class/input") / sysname;
  std::string candidate_path;
  for (int attempt = 0; attempt < 100; ++attempt) {
    std::error_code error_code;
    if (std::filesystem::exists(sys_path, error_code)) {
      for (const auto &entry : std::filesystem::directory_iterator(
               sys_path,
               std::filesystem::directory_options::skip_permission_denied,
               error_code)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("event", 0) == 0) {
          candidate_path = "/dev/input/" + name;

          // 节点会先出现，udev 随后才应用 input 组权限
          if (::access(candidate_path.c_str(), R_OK | W_OK) == 0) {
            event_path_ = candidate_path;
            return true;
          }
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (candidate_path.empty()) {
    error_ =
        "virtual input event device did not appear under " + sys_path.string();
  } else {
    error_ = "virtual input event device is not readable and writable: " +
             candidate_path;
  }
  return false;
}

// 写入一个轴、按键或同步事件
bool InputDeviceSimulator::emit(std::uint16_t type, std::uint16_t code,
                                std::int32_t value) {
  input_event event{};
  event.type = type;
  event.code = code;
  event.value = value;
  if (::write(fd_, &event, sizeof(event)) ==
      static_cast<ssize_t>(sizeof(event))) {
    return true;
  }
  error_ = systemError("write virtual input event");
  return false;
}

// 将 Web 原始报告完整写入虚拟输入设备
bool InputDeviceSimulator::writeReport(const VirtualG29Report &report) {
  if (fd_ < 0) {
    error_ = "virtual input device is not started";
    return false;
  }

  // 一帧 Web 数据拆成方向盘和踏板轴事件
  if (!emit(EV_ABS, ABS_X, report.steering_axis) ||
      !emit(EV_ABS, ABS_Y, report.throttle_axis) ||
      !emit(EV_ABS, ABS_Z, report.brake_axis) ||
      !emit(EV_ABS, ABS_RZ, report.clutch_axis)) {
    return false;
  }

  // Linux 使用两个绝对轴表示方向帽
  std::int32_t hat_x = 0;
  std::int32_t hat_y = 0;
  povToHat(report.pov, hat_x, hat_y);
  if (!emit(EV_ABS, ABS_HAT0X, hat_x) || !emit(EV_ABS, ABS_HAT0Y, hat_y)) {
    return false;
  }

  // 具名按钮状态转换为 Linux 输入事件
  for (const ButtonMapping &button : kButtonMappings) {
    if (!emit(EV_KEY, button.code, report.*(button.pressed) ? 1 : 0)) {
      return false;
    }
  }

  // 一帧写完后发送同步事件
  return emit(EV_SYN, SYN_REPORT, 0);
}

// 销毁内核虚拟设备，并释放 uinput fd
void InputDeviceSimulator::stop() {
  event_path_.clear();
  if (fd_ < 0) return;
  ioctl(fd_, UI_DEV_DESTROY);
  close(fd_);
  fd_ = -1;
}
