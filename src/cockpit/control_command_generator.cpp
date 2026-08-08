#include "cockpit/control_command_generator.h"

#include <cmath>

namespace {

// 控制规则参数
constexpr double kMaxSteeringDegrees = 90.0;
constexpr double kGearShiftBrakeThreshold = 0.20;
constexpr double kGearShiftMaxSpeed = 0.5;
constexpr double kParkingClutchThreshold = 0.20;
constexpr double kEmergencyBrakeThreshold = 0.20;
constexpr auto kHoldDuration = std::chrono::milliseconds(1000);
constexpr auto kRemoteRotationTimeout = std::chrono::milliseconds(5000);
constexpr int kRemoteRotationThreshold = 12;

// 判断方向帽是否包含向上方向
bool povUp(PovDirection pov) {
  return pov == PovDirection::UP || pov == PovDirection::UP_LEFT ||
         pov == PovDirection::UP_RIGHT;
}

// 判断方向帽是否包含向左方向
bool povLeft(PovDirection pov) {
  return pov == PovDirection::LEFT || pov == PovDirection::UP_LEFT ||
         pov == PovDirection::DOWN_LEFT;
}

// 判断方向帽是否包含向右方向
bool povRight(PovDirection pov) {
  return pov == PovDirection::RIGHT || pov == PovDirection::UP_RIGHT ||
         pov == PovDirection::DOWN_RIGHT;
}

// 将布尔目标转换为三态开关指令
SwitchCommand switchCommand(bool enabled) {
  return enabled ? SwitchCommand::ON : SwitchCommand::OFF;
}

// 计算三态指令对应的目标开关状态
bool requestedState(SwitchCommand command, bool actual_state) {
  if (command == SwitchCommand::ON) return true;
  if (command == SwitchCommand::OFF) return false;
  return actual_state;
}

// 基于实际状态切换目标开关指令
void toggleSwitch(SwitchCommand &command, bool actual_state) {
  command = switchCommand(!requestedState(command, actual_state));
}

// 实际状态确认后清除待发送开关指令
void clearConfirmedSwitch(SwitchCommand &command, bool actual_state) {
  if ((command == SwitchCommand::ON && actual_state) ||
      (command == SwitchCommand::OFF && !actual_state)) {
    command = SwitchCommand::NO_CTL;
  }
}

}  // namespace

// 消费新输入并更新边沿和按住状态
void ControlCommandGenerator::updateInput(
    const InputDeviceState &state, Clock::time_point now) {
  previous_ = current_;
  current_ = state;
  has_input_ = true;

  // 需要锁存的指令在输入到达时立即处理，避免丢失短按
  updateGear();
  updateToggleControls();
  updateRemoteMode(now);

  // 记录或清理驻车长按状态
  const bool parking_combo =
      current_.clutch_pedal > kParkingClutchThreshold &&
      current_.l1_pressed && !current_.r1_pressed;
  if (!parking_combo) {
    parking_holding_ = false;
    parking_action_done_ = false;
  } else if (!parking_holding_) {
    parking_holding_ = true;
    parking_hold_start_ = now;
  }

  // 记录或清理急停长按状态
  const bool emergency_combo =
      current_.brake_pedal > kEmergencyBrakeThreshold &&
      current_.r1_pressed;
  if (!emergency_combo) {
    emergency_holding_ = false;
    emergency_action_done_ = false;
  } else if (!emergency_holding_) {
    emergency_holding_ = true;
    emergency_hold_start_ = now;
  }
}

// 基于最新输入和当前时间生成待发送指令
RemoteCtlCmd ControlCommandGenerator::generate(Clock::time_point now) {
  if (!has_input_) return command_;

  updateContinuousControls();
  updateBucket();

  // 一次按住只能触发一次驻车或急停切换
  if (parking_holding_ && !parking_action_done_ &&
      now - parking_hold_start_ >= kHoldDuration) {
    toggleSwitch(command_.parking, actual_state_.parking);
    parking_action_done_ = true;
  }
  if (emergency_holding_ && !emergency_action_done_ &&
      now - emergency_hold_start_ >= kHoldDuration) {
    toggleSwitch(command_.remote_emergency, actual_state_.emergency);
    emergency_action_done_ = true;
  }

  return command_;
}

// 保存车辆实际状态
void ControlCommandGenerator::syncVehicleState(const RemoteDrivingState &state) {
  // 保存最新车辆状态
  actual_state_ = state;
  has_actual_state_ = true;

  // 状态确认后停止重复发送模式切换
  const bool enter_confirmed =
      command_.remoteMode == RemoteMode::REMOTE_ENTER &&
      state.remoteMode == DriveMode::REMOTE;
  const bool exit_confirmed =
      command_.remoteMode == RemoteMode::REMOTE_EXIT &&
      state.remoteMode != DriveMode::REMOTE;
  if (enter_confirmed || exit_confirmed)
    command_.remoteMode = RemoteMode::REMOTE_NO_CONTROL;

  // 开关命令被实车状态确认后恢复为不控制，避免覆盖其他控制端
  clearConfirmedSwitch(command_.parking, state.parking);
  clearConfirmedSwitch(command_.horn, state.horn);
  clearConfirmedSwitch(command_.spray, state.spray);
  clearConfirmedSwitch(command_.remote_emergency, state.emergency);
  clearConfirmedSwitch(command_.window_wiper, state.window_wiper);
  clearConfirmedSwitch(command_.light_brake, state.light_brake);
  clearConfirmedSwitch(command_.light_position, state.light_position);
  clearConfirmedSwitch(command_.light_near, state.light_near);
  clearConfirmedSwitch(command_.light_far, state.light_far);
  clearConfirmedSwitch(command_.light_turn_left, state.light_turn_left);
  clearConfirmedSwitch(command_.light_turn_right, state.light_turn_right);
  clearConfirmedSwitch(command_.light_working_rear,
                       state.light_working_rear);
  clearConfirmedSwitch(command_.light_danger, state.light_danger);
  clearConfirmedSwitch(command_.light_reverse, state.light_reverse);
  clearConfirmedSwitch(command_.light_double_flash,
                       state.light_double_flash);
  clearConfirmedSwitch(command_.light_front, state.light_front);
  clearConfirmedSwitch(command_.light_working_side,
                       state.light_working_side);
  clearConfirmedSwitch(command_.light_fog, state.light_fog);
  clearConfirmedSwitch(command_.diff_lock, state.diff_lock);
}

// 判断按键上升沿
bool ControlCommandGenerator::rose(bool current, bool previous) {
  return current && !previous;
}

// 更新转向踏板与持续型控制
void ControlCommandGenerator::updateContinuousControls() {
  command_.steering_angle = current_.wheel * kMaxSteeringDegrees;
  command_.acc_pedal = current_.accelerator_pedal * 100.0;
  command_.brake_pedal = current_.brake_pedal * 100.0;
  command_.horn = switchCommand(povUp(current_.pov));
  command_.spray = switchCommand(current_.r2_pressed);
  command_.light_brake = switchCommand(command_.brake_pedal > 0);
}

// 处理制动与挡位组合
void ControlCommandGenerator::updateGear() {
  // 判断车辆是否满足换挡条件
  const bool vehicle_stopped =
      !has_actual_state_ || std::abs(actual_state_.speed) <= kGearShiftMaxSpeed;
  const bool can_shift =
      current_.brake_pedal > kGearShiftBrakeThreshold && vehicle_stopped;
  if (!can_shift) return;

  // 将组合按键映射为目标挡位
  if (current_.select_pressed ||
      (current_.l3_pressed && current_.r3_pressed)) {
    command_.gear = GearInfo::NEUTRAL;
  } else if (current_.l3_pressed) {
    command_.gear = GearInfo::DRIVE_1;
  } else if (current_.r3_pressed) {
    command_.gear = GearInfo::REVERSE_1;
  }
  // 根据当前挡位联动倒车灯
  command_.light_reverse =
      switchCommand(command_.gear == GearInfo::REVERSE_1);
}

// 处理铲斗按钮状态
void ControlCommandGenerator::updateBucket() {
  command_.bucket_info =
      current_.plus_pressed && !current_.minus_pressed
          ? BucketInfo::BUCKET_UP
          : (!current_.plus_pressed && current_.minus_pressed
                 ? BucketInfo::BUCKET_DOWN
                 : BucketInfo::BUCKET_KEEP);
}

// 处理单次按下切换
void ControlCommandGenerator::updateToggleControls() {
  // 切换雨刷与差速锁
  if (rose(current_.l2_pressed, previous_.l2_pressed))
    toggleSwitch(command_.window_wiper, actual_state_.window_wiper);

  if (rose(current_.square_pressed, previous_.square_pressed))
    toggleSwitch(command_.light_fog, actual_state_.light_fog);
  if (rose(current_.start_pressed, previous_.start_pressed))
    toggleSwitch(command_.diff_lock, actual_state_.diff_lock);

  // 切换近光与远光
  const bool near_changed =
      rose(current_.cross_pressed, previous_.cross_pressed);
  const bool far_changed =
      rose(current_.triangle_pressed, previous_.triangle_pressed);
  if (near_changed)
    toggleSwitch(command_.light_near, actual_state_.light_near);
  if (far_changed)
    toggleSwitch(command_.light_far, actual_state_.light_far);

  // 近光或远光开启时联动灯组
  if (near_changed || far_changed) {
    const bool light_group =
        requestedState(command_.light_near, actual_state_.light_near) ||
        requestedState(command_.light_far, actual_state_.light_far);
    command_.light_position = switchCommand(light_group);
    command_.light_working_rear = switchCommand(light_group);
    command_.light_danger = switchCommand(light_group);
    command_.light_front = switchCommand(light_group);
    command_.light_working_side = switchCommand(light_group);
  }

  // 切换双闪与转向灯
  if (rose(current_.circle_pressed, previous_.circle_pressed))
    toggleSwitch(command_.light_double_flash,
                 actual_state_.light_double_flash);
  if (rose(povLeft(current_.pov), povLeft(previous_.pov)))
    toggleSwitch(command_.light_turn_left, actual_state_.light_turn_left);
  if (rose(povRight(current_.pov), povRight(previous_.pov)))
    toggleSwitch(command_.light_turn_right, actual_state_.light_turn_right);
}

// 处理旋钮进入与退出远控
void ControlCommandGenerator::updateRemoteMode(Clock::time_point now) {
  // 超时后丢弃未完成的旋钮序列
  if (last_remote_rotation_ != Clock::time_point{} &&
      now - last_remote_rotation_ > kRemoteRotationTimeout) {
    resetRemoteRotation();
  }

  // 检测两个旋转方向的按键边沿
  const bool clockwise =
      rose(current_.encoder_clockwise_pressed,
           previous_.encoder_clockwise_pressed);
  const bool counter_clockwise =
      rose(current_.encoder_counter_clockwise_pressed,
           previous_.encoder_counter_clockwise_pressed);
  // 累计单一方向的连续旋转次数
  if (clockwise && counter_clockwise) {
    resetRemoteRotation();
  } else if (clockwise) {
    ++remote_cw_count_;
    remote_ccw_count_ = 0;
    last_remote_rotation_ = now;
  } else if (counter_clockwise) {
    ++remote_ccw_count_;
    remote_cw_count_ = 0;
    last_remote_rotation_ = now;
  }

  // 确认键按下后提交模式切换
  if (!rose(current_.encoder_confirm_pressed,
            previous_.encoder_confirm_pressed)) {
    return;
  }
  if (remote_cw_count_ >= kRemoteRotationThreshold) {
    resetCommandForRemoteEntry();
    command_.remoteMode = RemoteMode::REMOTE_ENTER;
  } else if (remote_ccw_count_ >= kRemoteRotationThreshold) {
    command_.remoteMode = RemoteMode::REMOTE_EXIT;
  }
  resetRemoteRotation();
}

// 清除上一轮远控遗留的挡位、踏板和辅助开关
void ControlCommandGenerator::resetCommandForRemoteEntry() {
  command_ = RemoteCtlCmd{};
  parking_holding_ = false;
  parking_action_done_ = false;
  parking_hold_start_ = {};
  emergency_holding_ = false;
  emergency_action_done_ = false;
  emergency_hold_start_ = {};
}

// 清空旋钮序列状态
void ControlCommandGenerator::resetRemoteRotation() {
  remote_cw_count_ = 0;
  remote_ccw_count_ = 0;
  last_remote_rotation_ = {};
}

// 清空全部映射状态
void ControlCommandGenerator::reset() { *this = ControlCommandGenerator{}; }
