#include "control_command_generator.h"

#include <cmath>

namespace {

namespace pb = remote_drive::protocol;

// 控制规则参数
constexpr double kMaxSteeringDegrees = 90.0;
constexpr double kGearShiftBrakeThreshold = 0.20;
constexpr double kGearShiftMaxSpeed = 0.5;
constexpr double kParkingClutchThreshold = 0.20;
constexpr double kEmergencyBrakeThreshold = 0.20;
constexpr auto kHoldDuration = std::chrono::milliseconds(500);
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
pb::SwitchCommand switchCommand(bool enabled) {
  return enabled ? pb::SWITCH_ON : pb::SWITCH_OFF;
}

// 将按住型输入转换为控制意图；松开表示当前控制源不参与
pb::HoldCommand holdCommand(bool pressed) {
  return pressed ? pb::HOLD_COMMAND_ON : pb::HOLD_COMMAND_NO_CONTROL;
}

// 计算三态指令对应的目标开关状态
bool requestedState(pb::SwitchCommand command, bool actual_state) {
  if (command == pb::SWITCH_ON)
    return true;
  if (command == pb::SWITCH_OFF)
    return false;
  return actual_state;
}

// 基于实际状态切换目标开关指令
pb::SwitchCommand toggleSwitch(pb::SwitchCommand command, bool actual_state) {
  return switchCommand(!requestedState(command, actual_state));
}

// 实际状态确认后清除待发送开关指令
pb::SwitchCommand clearConfirmedSwitch(pb::SwitchCommand command,
                                       bool actual_state) {
  if ((command == pb::SWITCH_ON && actual_state) ||
      (command == pb::SWITCH_OFF && !actual_state)) {
    return pb::SWITCH_NO_CONTROL;
  }
  return command;
}

// 实车挡位确认后清除待发送挡位指令
pb::GearCommand clearConfirmedGear(pb::GearCommand command,
                                   pb::GearState actual_gear) {
  const bool confirmed =
      (command == pb::GEAR_COMMAND_NEUTRAL &&
       actual_gear == pb::GEAR_STATE_NEUTRAL) ||
      (command == pb::GEAR_COMMAND_REVERSE &&
       actual_gear == pb::GEAR_STATE_REVERSE) ||
      (command == pb::GEAR_COMMAND_DRIVE &&
       actual_gear == pb::GEAR_STATE_DRIVE);
  return confirmed ? pb::GEAR_COMMAND_NO_CONTROL : command;
}

} // namespace

ControlCommandGenerator::ControlCommandGenerator() {
  vehicle_state_.set_parking(true);
}

// 处理最新输入状态，更新控制指令、按键边沿和累计状态
void ControlCommandGenerator::processInputState(
    const InputDeviceState &state, Clock::time_point now) {
  previous_input_ = current_input_;
  current_input_ = state;

  // 需要锁存的指令在输入到达时立即处理，避免丢失短按
  updateGear();
  updateToggleControls();
  updateRemoteModeRequest(now);

  // 输入发生变化时立即刷新持续型控制和铲斗状态
  updateContinuousControls();
  updateBucket();

  // 驻车组合条件从不成立变为成立时开始计时，松开后清除计时
  const bool previous_parking_combo =
      previous_input_.clutch_pedal > kParkingClutchThreshold &&
      previous_input_.l1_pressed && !previous_input_.r1_pressed;
  const bool parking_combo =
      current_input_.clutch_pedal > kParkingClutchThreshold &&
      current_input_.l1_pressed && !current_input_.r1_pressed;
  if (!parking_combo) {
    parking_hold_start_.reset();
  } else if (!previous_parking_combo) {
    parking_hold_start_ = now;
  }

  // 急停组合条件从不成立变为成立时开始计时，松开后清除计时
  const bool previous_emergency_combo =
      previous_input_.brake_pedal > kEmergencyBrakeThreshold &&
      previous_input_.r1_pressed;
  const bool emergency_combo =
      current_input_.brake_pedal > kEmergencyBrakeThreshold &&
      current_input_.r1_pressed;
  if (!emergency_combo) {
    emergency_hold_start_.reset();
  } else if (!previous_emergency_combo) {
    emergency_hold_start_ = now;
  }
}

// 结算长按等时间状态并返回最新控制指令
pb::ControlCommand
ControlCommandGenerator::generateCommand(Clock::time_point now) {
  // 超时触发后清空开始时间，保持按住不会再次产生上升沿
  if (parking_hold_start_ &&
      now - *parking_hold_start_ >= kHoldDuration) {
    command_.set_parking(
        toggleSwitch(command_.parking(), vehicle_state_.parking()));
    parking_hold_start_.reset();
  }
  if (emergency_hold_start_ &&
      now - *emergency_hold_start_ >= kHoldDuration) {
    command_.set_remote_emergency(
        toggleSwitch(command_.remote_emergency(), vehicle_state_.emergency()));
    emergency_hold_start_.reset();
  }

  return command_;
}

// 生成退出远控指令
pb::ControlCommand ControlCommandGenerator::generateExitCommand() const {
  pb::ControlCommand command;
  command.set_remote_mode_request(pb::REMOTE_MODE_REQUEST_EXIT);
  return command;
}

// 保存车辆实际状态
void ControlCommandGenerator::syncVehicleState(const pb::VehicleState &state) {
  vehicle_state_ = state;

  // 状态确认后停止重复发送模式切换
  const bool enter_confirmed =
      command_.remote_mode_request() == pb::REMOTE_MODE_REQUEST_ENTER &&
      state.drive_mode() == pb::DRIVE_MODE_REMOTE;
  const bool exit_confirmed =
      command_.remote_mode_request() == pb::REMOTE_MODE_REQUEST_EXIT &&
      state.drive_mode() != pb::DRIVE_MODE_REMOTE;
  if (enter_confirmed || exit_confirmed)
    command_.set_remote_mode_request(pb::REMOTE_MODE_REQUEST_NONE);

  // 挡位被实车状态确认后恢复为不控制，避免覆盖其他控制端
  command_.set_gear(clearConfirmedGear(command_.gear(), state.gear()));

  // 锁存型开关被实车状态确认后恢复为不控制，避免覆盖其他控制端；
  // 鸣笛、喷水和制动灯是输入持续型控制，始终保留当前输入对应的命令
  command_.set_parking(
      clearConfirmedSwitch(command_.parking(), state.parking()));
  command_.set_remote_emergency(clearConfirmedSwitch(
      command_.remote_emergency(), state.emergency()));
  command_.set_window_wiper(
      clearConfirmedSwitch(command_.window_wiper(), state.window_wiper()));
  command_.set_light_position(clearConfirmedSwitch(
      command_.light_position(), state.light_position()));
  command_.set_light_near(
      clearConfirmedSwitch(command_.light_near(), state.light_near()));
  command_.set_light_far(
      clearConfirmedSwitch(command_.light_far(), state.light_far()));
  command_.set_light_turn_left(clearConfirmedSwitch(
      command_.light_turn_left(), state.light_turn_left()));
  command_.set_light_turn_right(clearConfirmedSwitch(
      command_.light_turn_right(), state.light_turn_right()));
  command_.set_light_working_rear(clearConfirmedSwitch(
      command_.light_working_rear(), state.light_working_rear()));
  command_.set_light_danger(
      clearConfirmedSwitch(command_.light_danger(), state.light_danger()));
  command_.set_light_reverse(
      clearConfirmedSwitch(command_.light_reverse(), state.light_reverse()));
  command_.set_light_double_flash(clearConfirmedSwitch(
      command_.light_double_flash(), state.light_double_flash()));
  command_.set_light_front(
      clearConfirmedSwitch(command_.light_front(), state.light_front()));
  command_.set_light_working_side(clearConfirmedSwitch(
      command_.light_working_side(), state.light_working_side()));
  command_.set_light_fog(
      clearConfirmedSwitch(command_.light_fog(), state.light_fog()));
  command_.set_diff_lock(
      clearConfirmedSwitch(command_.diff_lock(), state.diff_lock()));
}

// 判断按键上升沿
bool ControlCommandGenerator::rose(bool current, bool previous) {
  return current && !previous;
}

// 更新转向踏板与持续型控制
void ControlCommandGenerator::updateContinuousControls() {
  const double brake_percent = current_input_.brake_pedal * 100.0;
  command_.set_steering_angle(current_input_.wheel * kMaxSteeringDegrees);
  command_.set_accelerator_percent(current_input_.accelerator_pedal * 100.0);
  command_.set_brake_percent(brake_percent);
  command_.set_horn(holdCommand(povUp(current_input_.pov)));
  command_.set_spray(holdCommand(current_input_.r2_pressed));
  command_.set_light_brake(switchCommand(brake_percent > 0));
}

// 处理制动与挡位组合
void ControlCommandGenerator::updateGear() {
  // 判断车辆是否满足换挡条件
  const bool vehicle_stopped =
      std::abs(vehicle_state_.speed()) <= kGearShiftMaxSpeed;
  const bool can_shift =
      current_input_.brake_pedal > kGearShiftBrakeThreshold && vehicle_stopped;
  if (!can_shift)
    return;

  // 将组合按键映射为目标挡位
  if (current_input_.select_pressed ||
      (current_input_.l3_pressed && current_input_.r3_pressed)) {
    command_.set_gear(pb::GEAR_COMMAND_NEUTRAL);
  } else if (current_input_.l3_pressed) {
    command_.set_gear(pb::GEAR_COMMAND_DRIVE);
  } else if (current_input_.r3_pressed) {
    command_.set_gear(pb::GEAR_COMMAND_REVERSE);
  } else {
    return;
  }
  // 根据当前挡位联动倒车灯
  command_.set_light_reverse(
      switchCommand(command_.gear() == pb::GEAR_COMMAND_REVERSE));
}

// 处理铲斗按钮状态
void ControlCommandGenerator::updateBucket() {
  command_.set_bucket(
      current_input_.plus_pressed && !current_input_.minus_pressed
          ? pb::BUCKET_COMMAND_UP
          : (!current_input_.plus_pressed && current_input_.minus_pressed
                 ? pb::BUCKET_COMMAND_DOWN
                 : pb::BUCKET_COMMAND_NO_CONTROL));
}

// 处理单次按下切换
void ControlCommandGenerator::updateToggleControls() {
  // 切换雨刷与差速锁
  if (rose(current_input_.l2_pressed, previous_input_.l2_pressed))
    command_.set_window_wiper(
        toggleSwitch(command_.window_wiper(), vehicle_state_.window_wiper()));

  if (rose(current_input_.square_pressed, previous_input_.square_pressed))
    command_.set_light_fog(
        toggleSwitch(command_.light_fog(), vehicle_state_.light_fog()));
  if (rose(current_input_.start_pressed, previous_input_.start_pressed))
    command_.set_diff_lock(
        toggleSwitch(command_.diff_lock(), vehicle_state_.diff_lock()));

  // 切换近光与远光
  const bool near_changed =
      rose(current_input_.cross_pressed, previous_input_.cross_pressed);
  const bool far_changed =
      rose(current_input_.triangle_pressed, previous_input_.triangle_pressed);
  if (near_changed)
    command_.set_light_near(
        toggleSwitch(command_.light_near(), vehicle_state_.light_near()));
  if (far_changed)
    command_.set_light_far(
        toggleSwitch(command_.light_far(), vehicle_state_.light_far()));

  // 近光或远光开启时联动灯组
  if (near_changed || far_changed) {
    const bool light_group =
        requestedState(command_.light_near(), vehicle_state_.light_near()) ||
        requestedState(command_.light_far(), vehicle_state_.light_far());
    command_.set_light_position(switchCommand(light_group));
    command_.set_light_working_rear(switchCommand(light_group));
    command_.set_light_danger(switchCommand(light_group));
    command_.set_light_front(switchCommand(light_group));
    command_.set_light_working_side(switchCommand(light_group));
  }

  // 切换双闪与转向灯
  if (rose(current_input_.circle_pressed, previous_input_.circle_pressed))
    command_.set_light_double_flash(toggleSwitch(
        command_.light_double_flash(), vehicle_state_.light_double_flash()));
  if (rose(povLeft(current_input_.pov), povLeft(previous_input_.pov)))
    command_.set_light_turn_left(toggleSwitch(
        command_.light_turn_left(), vehicle_state_.light_turn_left()));
  if (rose(povRight(current_input_.pov), povRight(previous_input_.pov)))
    command_.set_light_turn_right(toggleSwitch(
        command_.light_turn_right(), vehicle_state_.light_turn_right()));
}

// 处理旋钮进入与退出远控
void ControlCommandGenerator::updateRemoteModeRequest(Clock::time_point now) {
  // 超时后丢弃未完成的旋钮序列
  if (last_remote_rotation_ &&
      now - *last_remote_rotation_ > kRemoteRotationTimeout) {
    resetRemoteRotation();
  }

  // 检测两个旋转方向的按键边沿
  const bool clockwise =
      rose(current_input_.encoder_clockwise_pressed,
           previous_input_.encoder_clockwise_pressed);
  const bool counter_clockwise =
      rose(current_input_.encoder_counter_clockwise_pressed,
           previous_input_.encoder_counter_clockwise_pressed);
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
  if (!rose(current_input_.encoder_confirm_pressed,
            previous_input_.encoder_confirm_pressed)) {
    return;
  }
  if (remote_cw_count_ >= kRemoteRotationThreshold) {
    resetCommandForRemoteEntry();
    command_.set_remote_mode_request(pb::REMOTE_MODE_REQUEST_ENTER);
  } else if (remote_ccw_count_ >= kRemoteRotationThreshold) {
    command_.set_remote_mode_request(pb::REMOTE_MODE_REQUEST_EXIT);
  }
  resetRemoteRotation();
}

// 清除上一轮远控遗留的挡位、踏板和辅助开关
void ControlCommandGenerator::resetCommandForRemoteEntry() {
  command_ = pb::ControlCommand{};
  parking_hold_start_.reset();
  emergency_hold_start_.reset();
}

// 清空旋钮序列状态
void ControlCommandGenerator::resetRemoteRotation() {
  remote_cw_count_ = 0;
  remote_ccw_count_ = 0;
  last_remote_rotation_.reset();
}

// 清空全部映射状态
void ControlCommandGenerator::reset() { *this = ControlCommandGenerator{}; }
