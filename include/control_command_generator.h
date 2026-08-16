#pragma once

#include <chrono>
#include <optional>

#include "input_device_state.h"
#include "remote_drive.pb.h"

// 根据操作输入生成远程控制指令
class ControlCommandGenerator {
 public:
  using Clock = std::chrono::steady_clock;

  ControlCommandGenerator();

  // 处理最新输入状态，更新控制指令、按键边沿和累计状态
  void processInputState(const InputDeviceState &state,
                         Clock::time_point now = Clock::now());

  // 结算长按等时间状态并返回最新控制指令
  remote_drive::protocol::ControlCommand
  generate(Clock::time_point now = Clock::now());

  // 同步车辆实际状态
  void syncVehicleState(const remote_drive::protocol::ChassisState &state);

  // 清空全部映射状态
  void reset();

  // 判断是否已经收到输入数据
  bool hasInput() const { return has_input_; }

 private:
  // 判断按键上升沿
  static bool rose(bool current, bool previous);

  // 更新连续型控制
  void updateContinuousControls();

  // 更新挡位控制
  void updateGear();

  // 更新铲斗控制
  void updateBucket();

  // 更新开关型控制
  void updateToggleControls();

  // 更新远控模式请求
  void updateRemoteModeRequest(Clock::time_point now);

  // 按安全初始值准备新的远控会话
  void resetCommandForRemoteEntry();

  // 清空旋钮序列
  void resetRemoteRotation();

  InputDeviceState current_{};
  InputDeviceState previous_{};
  remote_drive::protocol::ControlCommand command_{};
  remote_drive::protocol::ChassisState actual_state_{};
  bool has_input_ = false;
  bool has_actual_state_ = false;

  std::optional<Clock::time_point> parking_hold_start_;
  std::optional<Clock::time_point> emergency_hold_start_;

  int remote_cw_count_ = 0;
  int remote_ccw_count_ = 0;
  Clock::time_point last_remote_rotation_{};
};
