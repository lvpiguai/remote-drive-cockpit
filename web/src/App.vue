<script setup>
import { computed, onMounted, onUnmounted, reactive, ref } from 'vue'

const cockpitProfiles = [
  { id: 'cockpit_01', name: '驾驶舱 01', websocketPort: 8765 },
  { id: 'cockpit_02', name: '驾驶舱 02', websocketPort: 8775 },
]
const requestedCockpitId = new URLSearchParams(window.location.search).get('cockpit')
const cockpitProfile = cockpitProfiles.find(
  (profile) => profile.id === requestedCockpitId,
) ?? cockpitProfiles[0]
const socketHost = window.location.hostname || '127.0.0.1'
const cockpitSocketUrl = `ws://${socketHost}:${cockpitProfile.websocketPort}`
const cockpitLinks = cockpitProfiles.map((profile) => ({
  ...profile,
  href: `/?cockpit=${profile.id}`,
}))
document.title = `${cockpitProfile.name} · Remote Drive`

const cockpitConnected = ref(false)
const controlSnapshot = ref(null)
const stateSnapshot = ref(null)
const vehicleStateSnapshots = reactive({})
const vehicles = ref([])
const selectedVehicleId = ref(null)
const pendingVehicleId = ref(null)
const activeView = ref('vehicles')

let cockpitSocket = null
let cockpitReconnectTimer = null
let stopped = false

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

function formatNumber(value, digits = 1) {
  return Number.isFinite(value) ? Number(value).toFixed(digits) : '--'
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
        }
        if (pendingVehicleId.value) {
          if (snapshot.selected === pendingVehicleId.value) {
            pendingVehicleId.value = null
            activeView.value = 'detail'
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

// 请求驾驶舱选择一台在线且未被其他驾驶舱占用的车辆
function selectVehicle(vehicle) {
  if (!vehicle.online || controlledByOtherCockpit(vehicle) ||
      pendingVehicleId.value ||
      cockpitSocket?.readyState !== WebSocket.OPEN) return
  pendingVehicleId.value = vehicle.id
  controlSnapshot.value = null
  stateSnapshot.value = null
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
}

// 根据车辆实际状态给出下一步远控操作提示
const driveHint = computed(() => {
  if (!selectedVehicle.value) return '请先选择在线车辆'
  if (!selectedVehicle.value.online) return '所选车辆已离线'
  if (!controlSnapshot.value || !stateSnapshot.value) return '等待控制链路就绪'
  if (stateSnapshot.value.mode !== 'REMOTE') return '请先进入远控模式'
  if (stateSnapshot.value.emergency) return '请先解除急停'
  if (stateSnapshot.value.parking) return '解除驻车后车辆才允许起步'
  if (stateSnapshot.value.gear === 'N') return '挂入行驶挡位后等待控制指令'
  if (controlSnapshot.value.brake > 0) return '请松开制动踏板'
  if (controlSnapshot.value.acc <= 0) return '车辆已就绪，等待油门输入'
  return '油门已生效，车辆正在加速'
})

onMounted(() => {
  connectCockpitSocket()
  window.addEventListener('pagehide', releaseSelectedVehicle)
})
onUnmounted(() => {
  stopped = true
  window.removeEventListener('pagehide', releaseSelectedVehicle)
  window.clearTimeout(cockpitReconnectTimer)
  releaseSelectedVehicle()
  cockpitSocket?.close()
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
        <span class="connection-state" :class="{ connected: cockpitConnected }">
          {{ cockpitConnected ? '驾驶舱已连接' : '驾驶舱未连接' }}
        </span>
        <span class="detail-online" :class="{ offline: !selectedVehicle?.online }">
          {{ selectedVehicle?.online ? '车辆在线' : '车辆离线' }}
        </span>
      </div>
    </header>

    <section class="layout">
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
              <span
                v-for="([key, label]) in controlSwitches"
                :key="key"
                :class="{ active: controlSnapshot[key] === 'ON' }"
              >{{ label }} · {{ controlSnapshot[key] }}</span>
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
              <span
                v-for="([key, label]) in stateSwitches"
                :key="key"
                :class="{ active: stateSnapshot[key] }"
              >{{ label }}</span>
            </div>
          </div>
          <div v-else class="telemetry-empty">等待车辆状态</div>
        </section>
      </aside>
    </section>
  </main>
</template>
