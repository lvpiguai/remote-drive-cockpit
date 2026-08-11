#pragma once

#include <linux/input.h>

#include <cstdint>
#include <string>
#include <vector>

#include "input_device_state.h"

namespace input_device {

// 输入轴原始范围，由 EVIOCGABS 从 eventX 读取
struct AxisRange {
  std::int32_t minimum = 0;
  std::int32_t maximum = 0;
};

// 累积轴和按键状态，收到 SYN_REPORT 后输出完整设备状态
class InputEventProcessor {
 public:
  // 保存指定控制轴的原始范围
  void setAxisRange(std::uint16_t code, AxisRange range);

  // 更新一个事件；遇到 SYN_REPORT 时输出完整状态并返回 true
  bool consume(const input_event &event, InputDeviceState &completed_state);

 private:
  InputDeviceState state_{};
  AxisRange steering_range_{};
  AxisRange throttle_range_{};
  AxisRange brake_range_{};
  AxisRange clutch_range_{};
  std::int32_t hat_x_ = 0;
  std::int32_t hat_y_ = 0;
};

// 输入设备读取器：从 /dev/input/eventX 读取输入数据
class InputDeviceReader {
 public:
  InputDeviceReader() = default;
  explicit InputDeviceReader(const std::string &path);
  ~InputDeviceReader();

  InputDeviceReader(const InputDeviceReader &) = delete;
  InputDeviceReader &operator=(const InputDeviceReader &) = delete;

  // 打开 eventX，读取轴范围，并请求设置 30% 自动回正强度
  bool openDevice(const std::string &path);

  // 非阻塞读取当前可用的完整输入帧
  std::vector<InputDeviceState> readAvailable();
  int fd() const { return fd_; }
  const std::string &path() const { return path_; }
  const std::string &error() const { return error_; }

 private:
  // 通过 EVIOCGABS 查询、校验并缓存四个控制轴的原始范围
  bool loadAxisRanges();

  int fd_ = -1;
  InputEventProcessor event_processor_;
  std::string path_;
  std::string error_;
};

}  // namespace input_device
