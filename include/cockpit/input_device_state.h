#pragma once

// 方向帽物理方向
enum class PovDirection {
  CENTER,
  UP,
  UP_RIGHT,
  RIGHT,
  DOWN_RIGHT,
  DOWN,
  DOWN_LEFT,
  LEFT,
  UP_LEFT,
};

// 输入设备归一化物理状态
struct InputDeviceState {
  double wheel = 0;              // -1=左满，0=回正，1=右满
  double accelerator_pedal = 0;  // 0=松开，1=踩到底
  double brake_pedal = 0;        // 0=松开，1=踩到底
  double clutch_pedal = 0;       // 0=松开，1=踩到底

  bool cross_pressed = false;
  bool square_pressed = false;
  bool circle_pressed = false;
  bool triangle_pressed = false;

  bool r1_pressed = false;
  bool l1_pressed = false;
  bool r2_pressed = false;
  bool l2_pressed = false;

  bool select_pressed = false;
  bool start_pressed = false;
  bool r3_pressed = false;
  bool l3_pressed = false;

  bool plus_pressed = false;
  bool minus_pressed = false;
  bool encoder_clockwise_pressed = false;
  bool encoder_counter_clockwise_pressed = false;
  bool encoder_confirm_pressed = false;

  PovDirection pov = PovDirection::CENTER;
};
