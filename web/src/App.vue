<script setup>
import { computed, onMounted, onUnmounted, reactive, ref, watch } from 'vue'

const cockpitProfiles = [
  { id: 'cockpit_01', name: '驾驶舱 01', websocketPort: 8765, simulatorPort: 8766 },
  { id: 'cockpit_02', name: '驾驶舱 02', websocketPort: 8775, simulatorPort: 8776 },
]
const requestedCockpitId = new URLSearchParams(window.location.search).get('cockpit')
const cockpitProfile = cockpitProfiles.find(
  (profile) => profile.id === requestedCockpitId,
) ?? cockpitProfiles[0]
const socketHost = window.location.hostname || '127.0.0.1'
const cockpitSocketUrl = `ws://${socketHost}:${cockpitProfile.websocketPort}`
const simulatorSocketUrl = `ws://${socketHost}:${cockpitProfile.simulatorPort}`
const cockpitLinks = cockpitProfiles.map((profile) => ({
  ...profile,
  href: `/?cockpit=${profile.id}`,
}))
document.title = `${cockpitProfile.name} · Remote Drive`

const wheelAngle = ref(0)
const throttle = ref(0)
const brake = ref(0)
const clutch = ref(0)
const buttonStates = reactive({
  cross_pressed: false,
  square_pressed: false,
  circle_pressed: false,
  triangle_pressed: false,
  r1_pressed: false,
  l1_pressed: false,
  r2_pressed: false,
  l2_pressed: false,
  select_pressed: false,
  start_pressed: false,
  r3_pressed: false,
  l3_pressed: false,
  plus_pressed: false,
  minus_pressed: false,
  encoder_clockwise_pressed: false,
  encoder_counter_clockwise_pressed: false,
  encoder_confirm_pressed: false,
  ps_pressed: false,
})
const pov = ref(0)
const rotaryPosition = ref(50)
const cockpitConnected = ref(false)
const simulatorConnected = ref(false)
const controlSnapshot = ref(null)
const stateSnapshot = ref(null)
const vehicleStateSnapshots = reactive({})
const vehicles = ref([])
const selectedVehicleId = ref(null)
const pendingVehicleId = ref(null)
const activeView = ref('vehicles')

const selectedVehicle = computed(() =>
  vehicles.value.find((vehicle) => vehicle.id === selectedVehicleId.value),
)
const sortedVehicles = computed(() => [...vehicles.value].sort((first, second) => {
  if (first.online !== second.online) return first.online ? -1 : 1
  return first.id.localeCompare(second.id, 'zh-CN', { numeric: true })
}))
const onlineVehicleCount = computed(() =>
  vehicles.value.filter((vehicle) => vehicle.online).length,
)

// 控制权归属于其他驾驶舱时，当前页面只展示占用信息
function controlledByOtherCockpit(vehicle) {
  return Boolean(vehicle.controller_id) &&
    vehicle.controller_id !== cockpitProfile.id
}

// 根据在线状态和控制权生成车辆卡片操作文案
function vehicleActionText(vehicle) {
  if (!vehicle.online) return '车辆不可用'
  if (controlledByOtherCockpit(vehicle)) {
    return `已由 ${vehicle.controller_id} 接管`
  }
  if (pendingVehicleId.value === vehicle.id) return '正在进入…'
  if (vehicle.controller_id === cockpitProfile.id) return '继续驾驶'
  return '进入驾驶详情'
}

// 将协议驾驶模式转换为页面中文文本
function driveModeText(mode) {
  return {
    MANUAL: '人工驾驶',
    STANDBY: '待机',
    REMOTE: '远程驾驶',
    AUTO: '自动驾驶',
  }[mode] || '--'
}

const controlSwitches = [
  ['parking', '驻车'], ['emergency', '急停'], ['horn', '喇叭'],
  ['spray', '喷水'], ['wiper', '雨刷'], ['brakeLight', '刹车灯'],
  ['positionLight', '位置灯'], ['lowBeam', '近光灯'], ['highBeam', '远光灯'],
  ['leftTurn', '左转灯'], ['rightTurn', '右转灯'], ['rearWorkLight', '后工作灯'],
  ['warningLight', '警示灯'], ['reverseLight', '倒车灯'], ['hazardLight', '双闪'],
  ['frontLight', '前照灯'], ['sideWorkLight', '侧工作灯'], ['fogLight', '雾灯'],
  ['diffLock', '差速锁'],
]

const stateSwitches = controlSwitches

const povDirections = {
  'D-UP': 1,
  'D-RIGHT': 3,
  'D-DOWN': 5,
  'D-LEFT': 7,
}

const steeringAxis = computed(() => {
  const scale = wheelAngle.value < 0 ? 32768 : 32767
  return Math.round((wheelAngle.value / 450) * scale)
})
const throttleAxis = computed(() => Math.round((throttle.value / 100) * 32767))
const brakeAxis = computed(() => Math.round((brake.value / 100) * 32767))
const clutchAxis = computed(() => Math.round((clutch.value / 100) * 32767))
const inputReport = computed(() => ({
  steering_axis: steeringAxis.value,
  throttle_axis: throttleAxis.value,
  brake_axis: brakeAxis.value,
  clutch_axis: clutchAxis.value,
  ...buttonStates,
  pov: pov.value,
}))

let cockpitSocket = null
let simulatorSocket = null
let cockpitReconnectTimer = null
let simulatorReconnectTimer = null
let buttonPulseTimer = null
let activePulseButton = null
let queuedClockwiseSteps = 0
let queuedCounterClockwiseSteps = 0
const buttonPulseQueue = []
let stopped = false

// 仅在驾驶详情页打开时向本驾驶舱的模拟器发送完整输入快照
function sendInputReport() {
  if (activeView.value === 'detail' &&
      simulatorSocket?.readyState === WebSocket.OPEN) {
    simulatorSocket.send(JSON.stringify(inputReport.value))
  }
}

// 离开当前控制页前主动释放车辆；WebSocket 断开和车端超时继续兜底
function releaseSelectedVehicle() {
  if (cockpitSocket?.readyState === WebSocket.OPEN) {
    cockpitSocket.send(JSON.stringify({ type: 'deselect_vehicle' }))
  }
}

// 建立驾驶舱 WebSocket，并用服务端快照驱动车辆列表和详情切换
function connectCockpitSocket() {
  const nextSocket = new WebSocket(cockpitSocketUrl)
  cockpitSocket = nextSocket
  nextSocket.addEventListener('open', () => {
    cockpitConnected.value = true
  })
  nextSocket.addEventListener('message', (event) => {
    try {
      const snapshot = JSON.parse(event.data)
      if (snapshot.type === 'control') {
        controlSnapshot.value = snapshot
        if (snapshot.remote === 'ENTER') resetInputControls()
      }
      if (snapshot.type === 'state') {
        vehicleStateSnapshots[snapshot.vehicle_id] = snapshot
        if (snapshot.vehicle_id === selectedVehicleId.value) {
          stateSnapshot.value = snapshot
        }
      }
      if (snapshot.type === 'vehicles') {
        const previousSelectedVehicleId = selectedVehicleId.value
        vehicles.value = snapshot.vehicles
        selectedVehicleId.value = snapshot.selected
        if (snapshot.selected !== previousSelectedVehicleId) {
          stateSnapshot.value = snapshot.selected
            ? vehicleStateSnapshots[snapshot.selected] ?? null
            : null
        }
        if (activeView.value === 'detail' && previousSelectedVehicleId &&
            !snapshot.selected) {
          activeView.value = 'vehicles'
          controlSnapshot.value = null
          stateSnapshot.value = null
          resetInputControls()
        }
        if (pendingVehicleId.value) {
          if (snapshot.selected === pendingVehicleId.value) {
            pendingVehicleId.value = null
            activeView.value = 'detail'
            window.requestAnimationFrame(sendInputReport)
          } else if (!snapshot.vehicles.some(
            (vehicle) => vehicle.id === pendingVehicleId.value &&
              vehicle.online && !controlledByOtherCockpit(vehicle),
          )) {
            pendingVehicleId.value = null
          }
        }
      }
    } catch {
      // 忽略非快照消息
    }
  })
  nextSocket.addEventListener('close', () => {
    if (cockpitSocket !== nextSocket) return
    cockpitSocket = null
    cockpitConnected.value = false
    activeView.value = 'vehicles'
    selectedVehicleId.value = null
    pendingVehicleId.value = null
    for (const vehicleId of Object.keys(vehicleStateSnapshots)) {
      delete vehicleStateSnapshots[vehicleId]
    }
    if (!stopped) {
      cockpitReconnectTimer = window.setTimeout(connectCockpitSocket, 1000)
    }
  })
  nextSocket.addEventListener('error', () => nextSocket.close())
}

// 建立当前驾驶舱独享的 G29 模拟器连接
function connectSimulatorSocket() {
  const nextSocket = new WebSocket(simulatorSocketUrl)
  simulatorSocket = nextSocket
  nextSocket.addEventListener('open', () => {
    simulatorConnected.value = true
    window.requestAnimationFrame(sendInputReport)
  })
  nextSocket.addEventListener('close', () => {
    if (simulatorSocket !== nextSocket) return
    simulatorSocket = null
    simulatorConnected.value = false
    if (!stopped) {
      simulatorReconnectTimer = window.setTimeout(connectSimulatorSocket, 1000)
    }
  })
  nextSocket.addEventListener('error', () => nextSocket.close())
}

watch(inputReport, sendInputReport)

onMounted(() => {
  connectCockpitSocket()
  connectSimulatorSocket()
  window.addEventListener('pagehide', releaseSelectedVehicle)
})
onUnmounted(() => {
  stopped = true
  window.removeEventListener('pagehide', releaseSelectedVehicle)
  window.clearTimeout(cockpitReconnectTimer)
  window.clearTimeout(simulatorReconnectTimer)
  window.clearTimeout(buttonPulseTimer)
  if (activePulseButton) setButton(activePulseButton, false)
  releaseSelectedVehicle()
  cockpitSocket?.close()
  simulatorSocket?.close()
})

function updateWheel(event) {
  wheelAngle.value = Number(event.target.value)
}

// 请求驾驶舱选择一台在线且未被其他驾驶舱占用的车辆
function selectVehicle(vehicle) {
  if (!vehicle.online || controlledByOtherCockpit(vehicle) ||
      pendingVehicleId.value ||
      cockpitSocket?.readyState !== WebSocket.OPEN) return
  pendingVehicleId.value = vehicle.id
  controlSnapshot.value = null
  stateSnapshot.value = null
  resetInputControls()
  cockpitSocket.send(JSON.stringify({ type: 'select_vehicle', vehicle_id: vehicle.id }))
}

// 主动释放当前车辆并返回车辆列表
function returnToVehicleList() {
  releaseSelectedVehicle()
  activeView.value = 'vehicles'
  selectedVehicleId.value = null
  pendingVehicleId.value = null
  controlSnapshot.value = null
  stateSnapshot.value = null
  resetInputControls()
}

function updatePedal(name, event) {
  if (name === 'throttle') throttle.value = Number(event.target.value)
  if (name === 'brake') brake.value = Number(event.target.value)
  if (name === 'clutch') clutch.value = Number(event.target.value)
}

// 将页面输入恢复为方向盘回正、踏板松开和按钮释放状态
function resetInputControls() {
  wheelAngle.value = 0
  throttle.value = 0
  brake.value = 0
  clutch.value = 0
  for (const field of Object.keys(buttonStates)) buttonStates[field] = false
  pov.value = 0
  rotaryPosition.value = 50
}

function setButton(field, pressed) {
  if (field in buttonStates) buttonStates[field] = pressed
}

// 依次生成短按脉冲，避免多个虚拟按钮边沿互相覆盖
function runButtonPulseQueue() {
  if (activePulseButton || buttonPulseQueue.length === 0) return

  activePulseButton = buttonPulseQueue.shift()
  setButton(activePulseButton, true)
  buttonPulseTimer = window.setTimeout(() => {
    setButton(activePulseButton, false)
    activePulseButton = null
    buttonPulseTimer = window.setTimeout(runButtonPulseQueue, 40)
  }, 40)
}

function queueButtonPulse(field) {
  buttonPulseQueue.push(field)
  if (field === 'encoder_confirm_pressed') {
    queuedClockwiseSteps = 0
    queuedCounterClockwiseSteps = 0
  }
  runButtonPulseQueue()
}

// 将旋钮位移拆成有上限的顺时针或逆时针脉冲
function queueRotarySteps(direction, count) {
  const clockwise = direction === 'encoder_clockwise_pressed'
  const queued = clockwise ? queuedClockwiseSteps : queuedCounterClockwiseSteps
  const added = Math.min(count, Math.max(0, 12 - queued))
  for (let index = 0; index < added; index += 1) {
    buttonPulseQueue.push(direction)
  }
  if (clockwise) queuedClockwiseSteps += added
  else queuedCounterClockwiseSteps += added
  runButtonPulseQueue()
}

function setPov(name) {
  pov.value = name ? povDirections[name] : 0
}

function updateRotary(event) {
  const nextPosition = Number(event.target.value)
  if (nextPosition === rotaryPosition.value) return
  const direction = nextPosition > rotaryPosition.value
    ? 'encoder_clockwise_pressed'
    : 'encoder_counter_clockwise_pressed'
  const steps = Math.abs(nextPosition - rotaryPosition.value)
  rotaryPosition.value = nextPosition
  queueRotarySteps(direction, steps)
}

function formatNumber(value, digits = 1) {
  return Number.isFinite(value) ? Number(value).toFixed(digits) : '--'
}

// 根据车辆实际状态给出下一步远控操作提示
const driveHint = computed(() => {
  if (!selectedVehicle.value) return '请先选择在线车辆'
  if (!selectedVehicle.value.online) return '所选车辆已离线'
  if (!controlSnapshot.value || !stateSnapshot.value) return '等待控制链路就绪'
  if (stateSnapshot.value.mode !== 'REMOTE') return '请先进入远控模式'
  if (stateSnapshot.value.emergency) return '请先解除急停'
  if (stateSnapshot.value.parking) return '离合踩过 20% 并长按 L1 一秒解除驻车'
  if (stateSnapshot.value.gear === 'N') return '踩住制动并按 L3 挂入 D1 挡'
  if (controlSnapshot.value.brake > 0) return '请松开制动踏板'
  if (controlSnapshot.value.acc <= 0) return '车辆已就绪，可以踩油门起步'
  return '油门已生效，车辆正在加速'
})

</script>

<template>
  <main v-if="activeView === 'vehicles'" class="vehicle-selection-page">
    <header class="selection-header">
      <div class="selection-brand">
        <span class="brand-mark">RD</span>
        <div>
          <strong>Remote Drive · {{ cockpitProfile.name }}</strong>
          <small>{{ cockpitProfile.id }} · 本地远程驾驶控制台</small>
        </div>
      </div>
      <div class="cockpit-page-actions">
        <nav class="cockpit-switcher" aria-label="驾驶舱页面">
          <a
            v-for="profile in cockpitLinks"
            :key="profile.id"
            :href="profile.href"
            :class="{ active: profile.id === cockpitProfile.id }"
          >{{ profile.name }}</a>
        </nav>
        <span class="connection-state" :class="{ connected: cockpitConnected }">
          {{ cockpitConnected ? '驾驶舱已连接' : '驾驶舱未连接' }}
        </span>
      </div>
    </header>

    <section class="selection-content">
      <div class="selection-intro">
        <div>
          <p class="selection-eyebrow">VEHICLE DIRECTORY</p>
          <h1>选择远程驾驶车辆</h1>
          <p>选择一台在线车辆进入驾驶详情页</p>
        </div>
        <div class="fleet-summary">
          <strong>{{ onlineVehicleCount }}</strong>
          <span>在线</span>
          <i></i>
          <strong>{{ vehicles.length }}</strong>
          <span>车辆</span>
        </div>
      </div>

      <div v-if="sortedVehicles.length" class="vehicle-card-grid">
        <button
          v-for="vehicle in sortedVehicles"
          :key="vehicle.id"
          type="button"
          class="fleet-card"
          :class="{
            offline: !vehicle.online,
            busy: controlledByOtherCockpit(vehicle),
            pending: pendingVehicleId === vehicle.id,
          }"
          :disabled="!vehicle.online || controlledByOtherCockpit(vehicle) || Boolean(pendingVehicleId)"
          @click="selectVehicle(vehicle)"
        >
          <span class="fleet-card-topline"></span>
          <span class="fleet-card-heading">
            <span class="fleet-vehicle-icon"><i></i></span>
            <span class="fleet-identity">
              <small>车辆编号</small>
              <strong>{{ vehicle.id }}</strong>
            </span>
            <span class="fleet-online" :class="{ offline: !vehicle.online }">
              {{ vehicle.online ? (controlledByOtherCockpit(vehicle) ? '占用' : '在线') : '离线' }}
            </span>
          </span>
          <span class="fleet-card-meta">
            <span><small>驾驶模式</small><strong>{{ driveModeText(vehicle.mode) }}</strong></span>
            <span><small>当前控制者</small><strong>{{ vehicle.controller_id || '空闲' }}</strong></span>
          </span>
          <span class="fleet-card-action">
            {{ vehicleActionText(vehicle) }}
            <b v-if="vehicle.online && !controlledByOtherCockpit(vehicle)">→</b>
          </span>
        </button>
      </div>

      <div v-else class="fleet-empty">
        <span class="fleet-empty-radar"></span>
        <strong>未指定车辆</strong>
        <p>启动驾驶舱时请提供车辆 ID</p>
      </div>
    </section>
  </main>

  <main v-else class="console-shell">
    <header class="detail-header">
      <button type="button" class="back-to-fleet" @click="returnToVehicleList">← 车辆列表</button>
      <div class="detail-vehicle-title">
        <small>{{ cockpitProfile.name }} · 当前选择车辆</small>
        <strong>{{ selectedVehicleId }}</strong>
      </div>
      <div class="detail-statuses">
        <span class="connection-state" :class="{ connected: simulatorConnected }">
          {{ simulatorConnected ? 'G29 模拟器已连接' : 'G29 模拟器未连接' }}
        </span>
        <span class="detail-online" :class="{ offline: !selectedVehicle?.online }">
          {{ selectedVehicle?.online ? '车辆在线' : '车辆离线' }}
        </span>
      </div>
    </header>
    <section class="layout">
      <section class="input-stage panel">
        <div class="wheel-area">
          <div class="operation-help">
            <button class="help-trigger" type="button" aria-describedby="operation-guide">
              <span>?</span> 操作说明
            </button>
            <section id="operation-guide" class="operation-guide" role="tooltip">
              <div class="guide-title">
                <strong>方向盘操作说明</strong>
                <span>移开鼠标自动关闭</span>
              </div>
              <div class="guide-columns">
                <div class="guide-group">
                  <h3>左侧控制</h3>
                  <dl>
                    <div><dt>方向键 ↑</dt><dd>喇叭</dd></div>
                    <div><dt>方向键 ← / →</dt><dd>左 / 右转向灯</dd></div>
                    <div><dt>L1</dt><dd>离合 &gt;20%，长按 1 秒切换驻车</dd></div>
                    <div><dt>L2</dt><dd>雨刷开关</dd></div>
                    <div><dt>L3</dt><dd>踩刹车 &gt;20% 挂 D1 挡</dd></div>
                    <div><dt>＋ / −</dt><dd>举斗 / 降斗</dd></div>
                  </dl>
                </div>
                <div class="guide-group">
                  <h3>右侧控制</h3>
                  <dl>
                    <div><dt>△ / ×</dt><dd>远光灯 / 近光灯</dd></div>
                    <div><dt>□ / ○</dt><dd>雾灯 / 双闪灯</dd></div>
                    <div><dt>R1</dt><dd>刹车 &gt;20%，长按 1 秒切换急停</dd></div>
                    <div><dt>R2</dt><dd>按住喷玻璃水</dd></div>
                    <div><dt>R3</dt><dd>踩刹车 &gt;20% 挂 R1 挡</dd></div>
                    <div><dt>Select / Start</dt><dd>N 挡 / 差速锁</dd></div>
                  </dl>
                </div>
                <div class="guide-group guide-driving">
                  <h3>远控与踏板</h3>
                  <dl>
                    <div><dt>顺时针 ≥12 格 + ↵</dt><dd>进入远控</dd></div>
                    <div><dt>逆时针 ≥12 格 + ↵</dt><dd>退出远控</dd></div>
                    <div><dt>左踏板</dt><dd>离合</dd></div>
                    <div><dt>中踏板</dt><dd>刹车</dd></div>
                    <div><dt>右踏板</dt><dd>油门</dd></div>
                  </dl>
                  <p>换挡前需踩刹车超过 20%，且车辆基本静止。</p>
                </div>
              </div>
            </section>
          </div>

          <div class="wheel" :style="{ transform: `rotate(${wheelAngle}deg)` }">
            <div class="rim"></div>
            <div class="spoke spoke-left"></div>
            <div class="spoke spoke-right"></div>
            <div class="spoke spoke-bottom"></div>
            <div class="center-hub">DEVICE</div>
            <div class="wheel-mark"></div>
          </div>

          <div class="control-cluster left-cluster">
            <div class="dpad">
              <button @pointerdown="setPov('D-UP')" @pointerup="setPov()" @pointerleave="setPov()">▲</button>
              <button @pointerdown="setPov('D-LEFT')" @pointerup="setPov()" @pointerleave="setPov()">◀</button>
              <button class="dpad-center">●</button>
              <button @pointerdown="setPov('D-RIGHT')" @pointerup="setPov()" @pointerleave="setPov()">▶</button>
              <button @pointerdown="setPov('D-DOWN')" @pointerup="setPov()" @pointerleave="setPov()">▼</button>
            </div>
            <button class="blue-button" :class="{ active: buttonStates.l1_pressed }" @pointerdown="setButton('l1_pressed', true)" @pointerup="setButton('l1_pressed', false)" @pointerleave="setButton('l1_pressed', false)">L1</button>
            <button class="blue-button" :class="{ active: buttonStates.l2_pressed }" @pointerdown="setButton('l2_pressed', true)" @pointerup="setButton('l2_pressed', false)" @pointerleave="setButton('l2_pressed', false)">L2</button>
            <button class="blue-button lower" :class="{ active: buttonStates.l3_pressed }" @pointerdown="setButton('l3_pressed', true)" @pointerup="setButton('l3_pressed', false)" @pointerleave="setButton('l3_pressed', false)">L3</button>
          </div>

          <div class="control-cluster right-cluster">
            <div class="face-buttons">
              <button @pointerdown="setButton('triangle_pressed', true)" @pointerup="setButton('triangle_pressed', false)" @pointerleave="setButton('triangle_pressed', false)">△</button>
              <button @pointerdown="setButton('square_pressed', true)" @pointerup="setButton('square_pressed', false)" @pointerleave="setButton('square_pressed', false)">□</button>
              <button @pointerdown="setButton('circle_pressed', true)" @pointerup="setButton('circle_pressed', false)" @pointerleave="setButton('circle_pressed', false)">○</button>
              <button @pointerdown="setButton('cross_pressed', true)" @pointerup="setButton('cross_pressed', false)" @pointerleave="setButton('cross_pressed', false)">×</button>
            </div>
            <button class="blue-button" :class="{ active: buttonStates.r1_pressed }" @pointerdown="setButton('r1_pressed', true)" @pointerup="setButton('r1_pressed', false)" @pointerleave="setButton('r1_pressed', false)">R1</button>
            <button class="blue-button" :class="{ active: buttonStates.r2_pressed }" @pointerdown="setButton('r2_pressed', true)" @pointerup="setButton('r2_pressed', false)" @pointerleave="setButton('r2_pressed', false)">R2</button>
            <button class="blue-button lower" :class="{ active: buttonStates.r3_pressed }" @pointerdown="setButton('r3_pressed', true)" @pointerup="setButton('r3_pressed', false)" @pointerleave="setButton('r3_pressed', false)">R3</button>
          </div>

          <div class="center-buttons">
            <button @pointerdown="setButton('select_pressed', true)" @pointerup="setButton('select_pressed', false)" @pointerleave="setButton('select_pressed', false)">Select</button>
            <button @pointerdown="setButton('start_pressed', true)" @pointerup="setButton('start_pressed', false)" @pointerleave="setButton('start_pressed', false)">Start</button>
            <button class="ps-button" @pointerdown="setButton('ps_pressed', true)" @pointerup="setButton('ps_pressed', false)" @pointerleave="setButton('ps_pressed', false)">PS</button>
          </div>

          <div class="bottom-wheel-controls">
            <div class="plus-minus">
              <button @pointerdown="setButton('plus_pressed', true)" @pointerup="setButton('plus_pressed', false)" @pointerleave="setButton('plus_pressed', false)">+</button>
              <button @pointerdown="setButton('minus_pressed', true)" @pointerup="setButton('minus_pressed', false)" @pointerleave="setButton('minus_pressed', false)">−</button>
            </div>
            <button class="rotary" @pointerdown="queueButtonPulse('encoder_confirm_pressed')">↵</button>
          </div>
        </div>

        <div class="input-sliders">
          <div class="wheel-slider">
            <label>方向盘 <strong>{{ wheelAngle.toFixed(0) }}°</strong></label>
            <input :value="wheelAngle" @input="updateWheel" type="range" min="-450" max="450" step="1" />
            <div><span>左满</span><button @click="wheelAngle = 0">回正</button><span>右满</span></div>
          </div>

          <div class="rotary-slider">
            <label>红色旋钮 <strong>{{ rotaryPosition - 50 }}</strong></label>
            <input :value="rotaryPosition" @input="updateRotary" type="range" min="0" max="100" step="1" />
            <div><span>逆时针</span><button @click="rotaryPosition = 50">居中</button><span>顺时针</span></div>
          </div>
        </div>

        <section class="cockpit-pedals">
          <div class="pedals">
            <label class="pedal clutch"><span>离合</span><input :value="clutch" @input="updatePedal('clutch', $event)" type="range" min="0" max="100" /><strong>{{ clutch.toFixed(0) }}%</strong></label>
            <label class="pedal brake"><span>制动</span><input :value="brake" @input="updatePedal('brake', $event)" type="range" min="0" max="100" /><strong>{{ brake.toFixed(0) }}%</strong></label>
            <label class="pedal throttle"><span>油门</span><input :value="throttle" @input="updatePedal('throttle', $event)" type="range" min="0" max="100" /><strong>{{ throttle.toFixed(0) }}%</strong></label>
          </div>
        </section>

      </section>

      <aside class="right-column">
        <section class="panel telemetry-panel">
          <div class="panel-title">
            <span>驾驶舱控制指令</span>
            <span class="connection-state" :class="{ connected: cockpitConnected }">
              {{ cockpitConnected ? '已连接' : '未连接' }}
            </span>
          </div>
          <div v-if="controlSnapshot" class="telemetry-body">
            <div class="metric-grid">
              <div><span>序号</span><strong>{{ controlSnapshot.seq }}</strong></div>
              <div><span>远控指令</span><strong>{{ controlSnapshot.remote }}</strong></div>
              <div><span>转向</span><strong>{{ formatNumber(controlSnapshot.steering) }}°</strong></div>
              <div><span>挡位</span><strong>{{ controlSnapshot.gear }}</strong></div>
              <div><span>油门</span><strong>{{ formatNumber(controlSnapshot.acc) }}%</strong></div>
              <div><span>制动</span><strong>{{ formatNumber(controlSnapshot.brake) }}%</strong></div>
              <div><span>铲斗</span><strong>{{ controlSnapshot.bucket }}</strong></div>
            </div>
            <div class="switch-grid">
              <span v-for="([key, label]) in controlSwitches" :key="key" :class="{ active: controlSnapshot[key] === 'ON' }">{{ label }} · {{ controlSnapshot[key] }}</span>
            </div>
          </div>
          <div v-else class="telemetry-empty">等待控制指令</div>
        </section>

        <section class="panel telemetry-panel state-panel">
          <div class="panel-title"><span>车辆回传状态</span></div>
          <div v-if="stateSnapshot" class="telemetry-body">
            <div class="metric-grid">
              <div><span>序号</span><strong>{{ stateSnapshot.seq }}</strong></div>
              <div><span>驾驶模式</span><strong>{{ stateSnapshot.mode }}</strong></div>
              <div><span>控制驾驶舱</span><strong>{{ stateSnapshot.controller_id || '空闲' }}</strong></div>
              <div><span>实际转向</span><strong>{{ formatNumber(stateSnapshot.steering) }}°</strong></div>
              <div><span>实际速度</span><strong>{{ formatNumber(stateSnapshot.speed) }}</strong></div>
              <div><span>挡位</span><strong>{{ stateSnapshot.gear }}</strong></div>
              <div><span>铲斗</span><strong>{{ stateSnapshot.bucket }}</strong></div>
            </div>
            <div class="drive-hint">{{ driveHint }}</div>
            <div class="switch-grid">
              <span v-for="([key, label]) in stateSwitches" :key="key" :class="{ active: stateSnapshot[key] }">{{ label }}</span>
            </div>
          </div>
          <div v-else class="telemetry-empty">等待车辆状态</div>
        </section>

      </aside>
    </section>
  </main>
</template>
