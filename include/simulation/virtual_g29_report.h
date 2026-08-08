#pragma once

#include <cstdint>
#include <string>

// Web 面板生成的 G29 虚拟硬件报告，不作为驾驶舱业务输入
struct VirtualG29Report {
  std::int32_t steering_axis = 0;  // -32768=左满，0=回正，32767=右满
  std::int32_t throttle_axis = 0;  // 0=完全松开，32767=踩到底
  std::int32_t brake_axis = 0;     // 0=完全松开，32767=踩到底
  std::int32_t clutch_axis = 0;    // 0=完全松开，32767=踩到底

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
  bool ps_pressed = false;

  std::uint8_t pov = 0;  // 方向帽：0=松开，1～8 从上方顺时针编号
};

// 解析并校验 Web 模拟器发来的完整报告
bool parseVirtualG29Report(const std::string &json, VirtualG29Report &report);
