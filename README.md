# Remote Drive Cockpit

驾驶舱独立项目，包含驾驶舱进程、G29 本地模拟器、输入设备采集和 Web 页面。

## 目录

```text
apps/     # cockpit 和 g29_simulator 入口
include/  # 驾驶舱、输入设备、协议和本地模拟器头文件
src/      # 实现
tests/    # 驾驶舱侧测试
web/      # 驾驶舱 Web 页面
```

`include/protocol/` 和 `src/protocol/` 是驾驶舱自己的协议实现。车端项目维护另一份
协议实现，两个项目不共享源码。

## 构建

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 运行

```bash
./build/g29_simulator --websocket-port 8766 ../.run/g29-event-path-cockpit_01
./build/cockpit --cockpit-id cockpit_01 \
  --vehicle-udp-port 7005 --websocket-port 8765 \
  /dev/input/eventX truck_01 truck_02 truck_03
```

Web 页面位于 `web/`，本地联调时可由顶层 `scripts/start.sh` 统一启动。
