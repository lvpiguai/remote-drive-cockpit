# Remote Drive Cockpit

驾驶舱独立项目，包含驾驶舱进程、真实输入设备采集、车辆选择页面和状态展示页面。

## 目录

```text
apps/     # cockpit 入口
include/  # 驾驶舱、输入设备和协议头文件
src/      # 实现
tests/    # 驾驶舱侧测试
web/      # 驾驶舱 Web 页面
```

`include/protocol/` 和 `src/protocol/` 是驾驶舱自己的协议实现。车端项目维护另一份
协议实现，两个项目不共享源码。

输入设备采集层位于 `include/devices/` 和 `src/devices/`。它通过 Linux evdev
从 `/dev/input/eventX` 非阻塞读取 `input_event`，使用 `EVIOCGABS` 获取轴范围，
并在收到 `SYN_REPORT` 后输出一帧完整的 `InputDeviceState`。

## 构建

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 运行

```bash
./build/cockpit --cockpit-id cockpit_01 \
  --vehicle-udp-port 7005 --websocket-port 8765 \
  /dev/input/eventX truck_01 truck_02 truck_03
```

Web 页面位于 `web/`，用于车辆选择和状态展示。控制输入来自驾驶舱进程读取到的
真实 Linux 输入设备。
