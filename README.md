# Remote Drive Cockpit

驾驶舱独立项目，包含驾驶舱进程、真实输入设备采集、车辆选择页面和状态展示页面。

## 目录

```text
apps/     # cockpit 入口
include/  # 头文件
src/      # C++ 实现
tests/    # C++ 测试
web/      # 驾驶舱 Web 页面
```

驾驶舱自己的协议、输入设备采集和核心流程都放在扁平的 `include/` 和 `src/`
目录下。车端项目维护另一份协议实现，两个项目不共享源码。

输入设备读取逻辑通过 Linux evdev 从 `/dev/input/eventX` 非阻塞读取
`input_event`，使用 `EVIOCGABS` 获取轴范围，并在收到 `SYN_REPORT` 后输出一帧
完整的 `InputDeviceState`。

## 构建

Ubuntu 需要安装 Boost.Beast 依赖：

```bash
sudo apt install libboost-dev libboost-system-dev
```

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 运行

```bash
./build/cockpit --cockpit-id cockpit_01 \
  --vehicle-udp-port 7005 --websocket-port 8765 \
  /dev/input/eventX
```

Web 页面位于 `web/`，用于车辆选择和状态展示。驾驶舱通过车辆状态动态发现车号
和车辆 UDP 地址，控制输入来自驾驶舱进程读取到的真实 Linux 输入设备。
