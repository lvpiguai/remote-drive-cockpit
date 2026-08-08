#pragma once

#include <chrono>

#include "cockpit/input_device_state.h"
#include "protocol/remote_control_protocol.h"

// 根据操作输入生成远程控制指令
class ControlCommandGenerator {
 public:
  using Clock = std::chrono::steady_clock;

  // 消费新输入并更新边沿和按住状态
  void updateInput(const InputDeviceState &state,
                   Clock::time_point now = Clock::now());

  // 基于最新输入和当前时间生成待发送指令
  RemoteCtlCmd generate(Clock::time_point now = Clock::now());

  // 同步车辆实际状态
  void syncVehicleState(const RemoteDrivingState &state);

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

  // 更新远控模式
  void updateRemoteMode(Clock::time_point now);

  // 按安全初始值准备新的远控会话
  void resetCommandForRemoteEntry();

  // 清空旋钮序列
  void resetRemoteRotation();

  InputDeviceState current_{};
  InputDeviceState previous_{};
  RemoteCtlCmd command_{};
  RemoteDrivingState actual_state_{};
  bool has_input_ = false;
  bool has_actual_state_ = false;

  bool parking_holding_ = false;
  bool parking_action_done_ = false;
  Clock::time_point parking_hold_start_{};
  bool emergency_holding_ = false;
  bool emergency_action_done_ = false;
  Clock::time_point emergency_hold_start_{};

  int remote_cw_count_ = 0;
  int remote_ccw_count_ = 0;
  Clock::time_point last_remote_rotation_{};
};
