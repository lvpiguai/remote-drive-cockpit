#include "simulation/virtual_g29_report.h"

#include <cctype>
#include <charconv>
#include <string>
#include <unordered_map>

namespace {

using Fields = std::unordered_map<std::string, std::string>;

// 跳过当前位置之后的连续空白字符
void skipWhitespace(const std::string &json, std::size_t &position) {
  while (position < json.size() &&
         std::isspace(static_cast<unsigned char>(json[position]))) {
    ++position;
  }
}

// 仅解析当前协议需要的扁平 JSON 对象：整数或布尔值
bool parseFields(const std::string &json, Fields &fields) {
  std::size_t position = 0;
  skipWhitespace(json, position);
  if (position >= json.size() || json[position++] != '{') return false;

  while (true) {
    skipWhitespace(json, position);
    if (position < json.size() && json[position] == '}') {
      ++position;
      break;
    }
    if (position >= json.size() || json[position++] != '"') return false;

    const std::size_t key_begin = position;
    while (position < json.size() && json[position] != '"') {
      if (json[position] == '\\') return false;
      ++position;
    }
    if (position >= json.size()) return false;
    const std::string key = json.substr(key_begin, position - key_begin);
    ++position;

    skipWhitespace(json, position);
    if (position >= json.size() || json[position++] != ':') return false;
    skipWhitespace(json, position);

    const std::size_t value_begin = position;
    if (json.compare(position, 4, "true") == 0) {
      position += 4;
    } else if (json.compare(position, 5, "false") == 0) {
      position += 5;
    } else {
      if (position < json.size() && json[position] == '-') ++position;
      const std::size_t digits_begin = position;
      while (position < json.size() &&
             std::isdigit(static_cast<unsigned char>(json[position]))) {
        ++position;
      }
      if (position == digits_begin) return false;
    }
    if (!fields.emplace(key, json.substr(value_begin, position - value_begin))
             .second) {
      return false;
    }

    skipWhitespace(json, position);
    if (position < json.size() && json[position] == ',') {
      ++position;
      continue;
    }
    if (position < json.size() && json[position] == '}') {
      ++position;
      break;
    }
    return false;
  }

  skipWhitespace(json, position);
  return position == json.size();
}

// 读取并校验指定范围内的整数字段
bool readInteger(const Fields &fields, const char *name, std::int64_t minimum,
                 std::int64_t maximum, std::int64_t &value) {
  const auto field = fields.find(name);
  if (field == fields.end()) return false;
  const char *begin = field->second.data();
  const char *end = begin + field->second.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end && value >= minimum &&
         value <= maximum;
}

// 读取指定的布尔字段
bool readBoolean(const Fields &fields, const char *name, bool &value) {
  const auto field = fields.find(name);
  if (field == fields.end()) return false;
  if (field->second == "true") {
    value = true;
    return true;
  }
  if (field->second == "false") {
    value = false;
    return true;
  }
  return false;
}

}  // namespace

// 将 Web JSON 报告解析为虚拟 G29 输入快照
bool parseVirtualG29Report(const std::string &json, VirtualG29Report &report) {
  Fields fields;
  if (!parseFields(json, fields) || fields.size() != 23) return false;

  VirtualG29Report parsed{};
  std::int64_t steering_axis = 0;
  std::int64_t throttle_axis = 0;
  std::int64_t brake_axis = 0;
  std::int64_t clutch_axis = 0;
  std::int64_t pov = 0;
  if (!readInteger(fields, "steering_axis", -32768, 32767, steering_axis) ||
      !readInteger(fields, "throttle_axis", 0, 32767, throttle_axis) ||
      !readInteger(fields, "brake_axis", 0, 32767, brake_axis) ||
      !readInteger(fields, "clutch_axis", 0, 32767, clutch_axis) ||
      !readBoolean(fields, "cross_pressed", parsed.cross_pressed) ||
      !readBoolean(fields, "square_pressed", parsed.square_pressed) ||
      !readBoolean(fields, "circle_pressed", parsed.circle_pressed) ||
      !readBoolean(fields, "triangle_pressed", parsed.triangle_pressed) ||
      !readBoolean(fields, "r1_pressed", parsed.r1_pressed) ||
      !readBoolean(fields, "l1_pressed", parsed.l1_pressed) ||
      !readBoolean(fields, "r2_pressed", parsed.r2_pressed) ||
      !readBoolean(fields, "l2_pressed", parsed.l2_pressed) ||
      !readBoolean(fields, "select_pressed", parsed.select_pressed) ||
      !readBoolean(fields, "start_pressed", parsed.start_pressed) ||
      !readBoolean(fields, "r3_pressed", parsed.r3_pressed) ||
      !readBoolean(fields, "l3_pressed", parsed.l3_pressed) ||
      !readBoolean(fields, "plus_pressed", parsed.plus_pressed) ||
      !readBoolean(fields, "minus_pressed", parsed.minus_pressed) ||
      !readBoolean(fields, "encoder_clockwise_pressed",
                   parsed.encoder_clockwise_pressed) ||
      !readBoolean(fields, "encoder_counter_clockwise_pressed",
                   parsed.encoder_counter_clockwise_pressed) ||
      !readBoolean(fields, "encoder_confirm_pressed",
                   parsed.encoder_confirm_pressed) ||
      !readBoolean(fields, "ps_pressed", parsed.ps_pressed) ||
      !readInteger(fields, "pov", 0, 8, pov)) {
    return false;
  }

  parsed.steering_axis = static_cast<std::int32_t>(steering_axis);
  parsed.throttle_axis = static_cast<std::int32_t>(throttle_axis);
  parsed.brake_axis = static_cast<std::int32_t>(brake_axis);
  parsed.clutch_axis = static_cast<std::int32_t>(clutch_axis);
  parsed.pov = static_cast<std::uint8_t>(pov);
  report = parsed;
  return true;
}
