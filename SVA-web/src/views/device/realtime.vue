<template>
  <div class="realtime-preview-container" ref="deviceContainer">
    <!-- 顶部控制工具栏 -->
    <div class="preview-header-bar">
      <div class="header-left">
        <span class="header-title">
          <i class="el-icon-video-camera"></i> 实时视频监控工作台
        </span>
        <el-radio-group v-model="activeView" size="small" style="margin-left: 20px;">
          <el-radio-button label="grid">
            <i class="el-icon-menu"></i> 分屏监控
          </el-radio-button>
          <el-radio-button label="table">
            <i class="el-icon-s-grid"></i> 设备列表
          </el-radio-button>
        </el-radio-group>
      </div>

      <div class="header-right" v-if="activeView === 'grid'">
        <span class="control-label">分屏模式：</span>
        <el-button-group size="small" class="split-button-group">
          <el-button :type="splitCount === 1 ? 'primary' : 'default'" @click="changeSplit(1)">1分屏</el-button>
          <el-button :type="splitCount === 4 ? 'primary' : 'default'" @click="changeSplit(4)">4分屏</el-button>
          <el-button :type="splitCount === 9 ? 'primary' : 'default'" @click="changeSplit(9)">9分屏</el-button>
        </el-button-group>

        <el-button
          size="small"
          icon="el-icon-circle-close"
          type="warning"
          plain
          style="margin-left: 12px;"
          @click="stopAllSlots"
        >全部停止</el-button>

        <el-button
          size="small"
          icon="el-icon-full-screen"
          type="info"
          plain
          @click="toggleFullscreen"
        >全屏</el-button>
      </div>
    </div>

    <!-- 视图 1：分屏监控工作台 -->
    <div class="split-workbench" v-show="activeView === 'grid'">
      <!-- 左侧设备选择抽屉/侧栏 -->
      <div class="device-sidebar">
        <div class="sidebar-header">
          <span class="sidebar-title">视频源列表 ({{ filteredDevices.length }})</span>
          <el-button type="text" icon="el-icon-refresh" size="mini" @click="getList">刷新</el-button>
        </div>

        <div class="sidebar-search">
          <el-input
            v-model="searchKeyword"
            placeholder="搜索设备名称/编码..."
            prefix-icon="el-icon-search"
            size="small"
            clearable
          />
        </div>

        <div class="sidebar-filter">
          <el-select v-model="filterStatus" size="mini" placeholder="状态筛选" style="width: 100%;">
            <el-option label="全部设备" value="" />
            <el-option label="运行中设备" value="RUNNING" />
            <el-option label="在线设备" value="online" />
          </el-select>
        </div>

        <div class="device-tree-list" v-loading="loading">
          <div
            v-for="device in filteredDevices"
            :key="device.ape_id"
            class="device-item-card"
            :class="{ 'is-active': isDevicePlaying(device.ape_id) }"
            @click="playDeviceInActiveSlot(device)"
          >
            <div class="device-item-main">
              <div class="device-name-row">
                <i class="el-icon-video-camera device-icon"></i>
                <span class="device-name" :title="device.name">{{ device.name }}</span>
                <span
                  class="online-dot"
                  :class="device.is_online === '1' ? 'is-online' : 'is-offline'"
                  :title="device.is_online === '1' ? '在线' : '离线'"
                ></span>
              </div>
              <div class="device-sub-row">
                <span class="device-code">{{ device.ape_id }}</span>
                <el-tag size="mini" :type="monitorStatusType(device)">{{ monitorStatusText(device) }}</el-tag>
              </div>
            </div>

            <div class="device-item-actions">
              <el-tooltip content="在当前窗口播放" placement="top">
                <el-button
                  type="primary"
                  icon="el-icon-video-play"
                  circle
                  size="mini"
                  @click.stop="playDeviceInActiveSlot(device)"
                ></el-button>
              </el-tooltip>
              <el-tooltip :content="getMonitorState(device) === 'RUNNING' ? '停止监控' : '启动监控'" placement="top">
                <el-button
                  :type="getMonitorState(device) === 'RUNNING' ? 'danger' : 'success'"
                  :icon="getMonitorState(device) === 'RUNNING' ? 'el-icon-video-pause' : 'el-icon-caret-right'"
                  circle
                  size="mini"
                  @click.stop="toggleDeviceMonitor(device)"
                ></el-button>
              </el-tooltip>
            </div>
          </div>

          <div v-if="filteredDevices.length === 0 && !loading" class="empty-device-hint">
            <i class="el-icon-info"></i>
            <p>暂无可预览设备</p>
            <span class="hint-sub">请在“设备管理”中添加摄像机并启动监控</span>
          </div>
        </div>
      </div>

      <!-- 右侧多宫格播放视口 -->
      <div class="grid-viewport" ref="gridContainer">
        <div class="slots-grid" :class="`grid-split-${splitCount}`">
          <div
            v-for="(slot, index) in activeSlots"
            :key="index"
            class="slot-wrapper"
            :class="{ 'is-selected': currentSlotIndex === index }"
            @click="selectSlot(index)"
          >
            <!-- 槽位标题栏 -->
            <div class="slot-header">
              <span class="slot-index-badge">窗口 {{ index + 1 }}</span>
              <span class="slot-title" :title="slot.title || '空闲'">
                {{ slot.title || '点击左侧列表添加' }}
              </span>
              <div class="slot-actions" v-if="slot.playUrl">
                <el-tooltip content="抓拍快照" placement="top">
                  <i class="el-icon-camera slot-btn" @click.stop="snapshotSlot(index)"></i>
                </el-tooltip>
                <el-tooltip content="关闭该路" placement="top">
                  <i class="el-icon-close slot-btn" @click.stop="closeSlot(index)"></i>
                </el-tooltip>
              </div>
            </div>

            <!-- 视频容器 -->
            <div class="slot-video-body">
              <video
                v-show="slot.playUrl"
                :ref="`videoSlot_${index}`"
                class="slot-video-element"
                muted
                autoplay
                playsinline
              ></video>

              <!-- 加载中遮罩 -->
              <div v-if="slot.loading" class="slot-overlay loading-overlay">
                <i class="el-icon-loading"></i>
                <span>连接视频流中...</span>
              </div>

              <!-- 错误或未播放空闲占位 -->
              <div v-if="slot.error" class="slot-overlay error-overlay">
                <i class="el-icon-warning-outline"></i>
                <span>{{ slot.error }}</span>
                <el-button size="mini" type="primary" plain style="margin-top: 8px;" @click.stop="retrySlot(index)">
                  重试连接
                </el-button>
              </div>

              <div v-if="!slot.playUrl && !slot.loading && !slot.error" class="slot-overlay idle-overlay">
                <i class="el-icon-video-camera-solid idle-icon"></i>
                <span class="idle-text">窗口 {{ index + 1 }} 就绪</span>
                <span class="idle-sub">点击左侧设备即可在此播放</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 视图 2：设备数据表格管理视图 (兼容原有功能) -->
    <div class="table-workbench" v-show="activeView === 'table'">
      <el-form :model="queryParams" ref="queryForm" size="small" :inline="true" label-width="68px">
        <el-form-item label="组织名称" prop="org_index">
          <el-select v-model="queryParams.org_index" filterable clearable placeholder="请选择组织名称" style="width: 200px">
            <el-option v-for="item in queryDeptOptions" :key="item.value" :label="item.label" :value="item.value"/>
          </el-select>
        </el-form-item>
        <el-form-item label="设备编码" prop="ape_id">
          <el-input v-model="queryParams.ape_id" placeholder="设备编码" clearable style="width: 180px" @keyup.enter.native="handleQuery"/>
        </el-form-item>
        <el-form-item label="设备名称" prop="name">
          <el-input v-model="queryParams.name" placeholder="设备名称" clearable style="width: 180px" @keyup.enter.native="handleQuery"/>
        </el-form-item>
        <el-form-item label="监控状态" prop="monitor_status">
          <el-select v-model="queryParams.monitor_status" clearable placeholder="监控状态" style="width: 140px">
            <el-option label="全部" value="" />
            <el-option label="运行中" value="RUNNING" />
            <el-option label="已停止" value="STOPPED" />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" icon="el-icon-search" size="mini" @click="handleQuery">搜索</el-button>
          <el-button icon="el-icon-refresh" size="mini" @click="resetQuery">重置</el-button>
        </el-form-item>
      </el-form>

      <el-table v-loading="loading" :data="deviceList" style="width: 100%" border>
        <el-table-column label="设备名称" prop="name" align="center" :show-overflow-tooltip="true" />
        <el-table-column label="设备编码" prop="ape_id" align="center" :show-overflow-tooltip="true" />
        <el-table-column label="IP地址" prop="ip_addr" align="center" :show-overflow-tooltip="true" />
        <el-table-column label="端口号" prop="port" align="center" width="90" />
        <el-table-column label="设备类型" prop="sub_type" align="center" width="100">
          <template slot-scope="scope">
            <span>{{ returnType(scope.row.sub_type) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="状态" prop="is_online" align="center" width="90">
          <template slot-scope="scope">
            <el-tag size="mini" :type="scope.row.is_online === '1' ? 'success' : 'info'">
              {{ scope.row.is_online === '1' ? '在线' : '离线' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="监控状态" align="center" width="100">
          <template slot-scope="scope">
            <el-tag size="mini" :type="monitorStatusType(scope.row)">{{ monitorStatusText(scope.row) }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" align="center" fixed="right" width="320">
          <template slot-scope="scope">
            <el-button
              size="mini"
              type="text"
              icon="el-icon-video-play"
              @click="handleStart(scope.row)"
            >启动监控</el-button>
            <el-button
              size="mini"
              type="text"
              icon="el-icon-video-pause"
              @click="handleStop(scope.row)"
            >停止监控</el-button>
            <el-button
              size="mini"
              type="text"
              icon="el-icon-video-camera"
              @click="handlePreviewInGrid(scope.row)"
            >切到分屏播放</el-button>
            <el-button
              size="mini"
              type="text"
              icon="el-icon-plus"
              @click="handleAddToWall(scope.row)"
            >加入监控墙</el-button>
          </template>
        </el-table-column>
      </el-table>

      <pagination
        v-show="total > 0"
        :total="total"
        :page.sync="queryParams.pageNum"
        :limit.sync="queryParams.pageSize"
        @pagination="getList"
      />
    </div>

    <!-- 弹窗播放器备用 -->
    <player
      v-show="viewProof"
      :viewProof="viewProof"
      :rtspUrl="modalRtspUrl"
      title="实时监控预览"
      @closeProof="viewProof = false"
    />
  </div>
</template>

<script>
import flvjs from 'flv.js'
import { deptTreeSelect } from '@/api/system/user'
import { getDeviceList, previewDeviceMonitor, startDeviceMonitor, stopDeviceMonitor } from '@/api/device'
import { upsertScreenWallStream } from '@/api/screenWall'
import player from '@/components/RTSPPlayer'
import { extractPlayableUrl, isBrowserPlayableUrl, isFlvUrl } from '@/utils/mediaPlayback'

export default {
  name: 'DeviceRealtimeMonitor',
  components: { player },
  data() {
    return {
      activeView: 'grid', // 'grid' (分屏监控) 或 'table' (设备列表)
      splitCount: 4, // 1, 4, 9
      currentSlotIndex: 0,
      loading: false,
      total: 0,
      deviceList: [],
      searchKeyword: '',
      filterStatus: '',
      slotsData: Array.from({ length: 9 }, () => ({
        deviceId: '',
        title: '',
        playUrl: '',
        flvPlayer: null,
        loading: false,
        error: ''
      })),
      autoPreviewInitialized: false,
      queryParams: {
        pageNum: 1,
        pageSize: 10,
        org_index: undefined,
        monitor_status: '',
        ape_id: undefined,
        name: undefined
      },
      deptOptions: undefined,
      queryDeptOptions: [],
      viewProof: false,
      modalRtspUrl: '',
      actionCooldownMap: {}
    }
  },
  computed: {
    activeSlots() {
      return this.slotsData.slice(0, this.splitCount)
    },
    filteredDevices() {
      return this.deviceList.filter(item => {
        if (this.searchKeyword) {
          const kw = this.searchKeyword.toLowerCase()
          const name = (item.name || '').toLowerCase()
          const code = (item.ape_id || '').toLowerCase()
          if (!name.includes(kw) && !code.includes(kw)) {
            return false
          }
        }
        if (this.filterStatus === 'RUNNING') {
          if (this.getMonitorState(item) !== 'RUNNING') return false
        } else if (this.filterStatus === 'online') {
          if (item.is_online !== '1') return false
        }
        return true
      })
    }
  },
  mounted() {
    this.getDeptTree()
    this.getList()
  },
  beforeDestroy() {
    this.stopAllSlots()
  },
  methods: {
    changeSplit(count) {
      this.splitCount = count
      if (this.currentSlotIndex >= count) {
        this.currentSlotIndex = 0
      }
      // 停止超出当前分屏范围的播放器
      for (let i = count; i < 9; i++) {
        this.closeSlot(i)
      }
    },
    selectSlot(index) {
      this.currentSlotIndex = index
    },
    isDevicePlaying(apeId) {
      return this.activeSlots.some(slot => slot.deviceId === apeId && slot.playUrl)
    },
    async playDeviceInActiveSlot(device) {
      const apeId = this.getApeId(device)
      if (!apeId) {
        this.$modal.msgError('设备编码不存在')
        return
      }

      const slotIndex = this.currentSlotIndex
      const slot = this.slotsData[slotIndex]
      this.closeSlot(slotIndex)

      slot.deviceId = apeId
      slot.title = device.name || apeId
      slot.loading = true
      slot.error = ''

      try {
        const response = await previewDeviceMonitor(apeId)
        const playUrl = extractPlayableUrl(response)

        if (!playUrl) {
          slot.loading = false
          slot.error = '暂无流地址，请检查设备流配置'
          return
        }

        slot.playUrl = playUrl
        slot.loading = false
        this.$nextTick(() => {
          this.initSlotPlayer(slotIndex, playUrl)
        })

        // 自动将选中光标移动到下一个槽位，方便快速连续点播
        if (this.currentSlotIndex < this.splitCount - 1) {
          this.currentSlotIndex += 1
        }
      } catch (err) {
        slot.loading = false
        slot.error = '获取视频流失败，请重试'
      }
    },
    initSlotPlayer(index, url) {
      const slot = this.slotsData[index]
      const videoRefs = this.$refs[`videoSlot_${index}`]
      const videoElement = Array.isArray(videoRefs) ? videoRefs[0] : videoRefs
      if (!videoElement || !url) return

      if (slot.flvPlayer) {
        try {
          slot.flvPlayer.unload()
          slot.flvPlayer.detachMediaElement()
          slot.flvPlayer.destroy()
        } catch (e) {
          // 播放器销毁失败时继续清理引用
        }
        slot.flvPlayer = null
      }

      if (!isBrowserPlayableUrl(url)) {
        videoElement.pause()
        videoElement.removeAttribute('src')
        videoElement.load()
        slot.playUrl = ''
        slot.error = '播放地址不可用'
        return
      }

      const isFlv = isFlvUrl(url)

      if (isFlv && flvjs.isSupported()) {
        try {
          const player = flvjs.createPlayer({
            type: 'flv',
            url: url,
            isLive: true,
            cors: true,
            hasAudio: false
          }, {
            enableWorker: false,
            enableStashBuffer: false,
            lazyLoad: false,
            stashInitialSize: 128,
            autoCleanupSourceBuffer: true,
            autoCleanupMaxBackwardDuration: 15,
            autoCleanupMinBackwardDuration: 5
          })
          player.attachMediaElement(videoElement)
          player.load()
          const playPromise = player.play()
          if (playPromise !== undefined) {
            playPromise.catch(() => {})
          }
          let retryTimer = null
          const reconnect = () => {
            if (slot.playUrl !== url) return
            if (retryTimer) clearTimeout(retryTimer)
            retryTimer = setTimeout(() => {
              if (slot.playUrl === url) {
                this.initSlotPlayer(index, url)
              }
            }, 1200)
          }
          player.on(flvjs.Events.ERROR, (errType, errDetail) => {
            console.warn(`[Realtime slot ${index}] FLV error:`, errType, errDetail)
            reconnect()
          })
          slot.flvPlayer = player
        } catch (err) {
          slot.error = 'FLV 播放初始化失败'
        }
      } else {
        videoElement.src = url
        videoElement.play().catch(() => {})
      }
    },
    closeSlot(index) {
      const slot = this.slotsData[index]
      if (!slot) return
      if (slot.flvPlayer) {
        try {
          slot.flvPlayer.unload()
          slot.flvPlayer.detachMediaElement()
          slot.flvPlayer.destroy()
        } catch (e) {
          // 播放器销毁失败时继续清理引用
        }
        slot.flvPlayer = null
      }
      const videoRefs = this.$refs[`videoSlot_${index}`]
      const videoElement = Array.isArray(videoRefs) ? videoRefs[0] : videoRefs
      if (videoElement) {
        videoElement.pause()
        videoElement.removeAttribute('src')
        videoElement.load()
      }
      slot.deviceId = ''
      slot.title = ''
      slot.playUrl = ''
      slot.loading = false
      slot.error = ''
    },
    stopAllSlots() {
      for (let i = 0; i < 9; i++) {
        this.closeSlot(i)
      }
      this.$modal.msgSuccess('已停止所有分屏播放')
    },
    retrySlot(index) {
      const slot = this.slotsData[index]
      if (!slot) return
      this.currentSlotIndex = index
      slot.error = ''
      slot.loading = true
      const matched = this.deviceList.find(d => this.getApeId(d) === slot.deviceId)
      if (matched) {
        this.playDeviceInActiveSlot(matched)
      } else if (slot.playUrl) {
        this.initSlotPlayer(index, slot.playUrl)
      } else {
        slot.loading = false
      }
    },
    snapshotSlot(index) {
      const videoRefs = this.$refs[`videoSlot_${index}`]
      const video = Array.isArray(videoRefs) ? videoRefs[0] : videoRefs
      if (!video || !video.videoWidth) {
        this.$modal.msgWarning('当前画面尚未加载完成，无法截图')
        return
      }
      try {
        const canvas = document.createElement('canvas')
        canvas.width = video.videoWidth
        canvas.height = video.videoHeight
        const ctx = canvas.getContext('2d')
        ctx.drawImage(video, 0, 0, canvas.width, canvas.height)
        const a = document.createElement('a')
        a.href = canvas.toDataURL('image/jpeg')
        a.download = `snapshot_${this.slotsData[index].title || 'camera'}_${Date.now()}.jpg`
        a.click()
        this.$modal.msgSuccess('抓拍成功并已下载')
      } catch (e) {
        this.$modal.msgWarning('截图失败，跨域限制或视频流异常')
      }
    },
    toggleFullscreen() {
      const elem = this.$refs.gridContainer
      if (!elem) return
      if (!document.fullscreenElement) {
        elem.requestFullscreen().catch(() => {})
      } else {
        document.exitFullscreen().catch(() => {})
      }
    },
    handlePreviewInGrid(row) {
      this.activeView = 'grid'
      this.playDeviceInActiveSlot(row)
    },
    async toggleDeviceMonitor(device) {
      const isRunning = this.getMonitorState(device) === 'RUNNING'
      if (isRunning) {
        await this.handleStop(device)
      } else {
        await this.handleStart(device)
      }
    },
    getDeptTree() {
      deptTreeSelect().then(response => {
        this.deptOptions = response.data || []
        this.queryDeptOptions = this.buildQueryDeptOptions(this.deptOptions)
      })
    },
    buildQueryDeptOptions(nodes, parentLabel = '') {
      const options = []
      ;(nodes || []).forEach(node => {
        const value = node.org_index || node.id
        const currentLabel = node.label || node.deptName || node.org_name
        if (value !== undefined && currentLabel) {
          const label = parentLabel ? `${parentLabel} / ${currentLabel}` : currentLabel
          options.push({ value, label })
          if (node.children && node.children.length) {
            options.push(...this.buildQueryDeptOptions(node.children, label))
          }
        } else if (node.children && node.children.length) {
          options.push(...this.buildQueryDeptOptions(node.children, parentLabel))
        }
      })
      return options
    },
    getApeId(row) {
      return row.ape_id || row.apeId || row.device_id || row.deviceId
    },
    getMonitorState(row) {
      return String(row.monitor_status || row.monitorStatus || 'STOPPED').toUpperCase()
    },
    monitorStatusText(row) {
      const state = this.getMonitorState(row)
      if (state === 'RUNNING') return '运行中'
      if (state === 'STARTING') return '启动中'
      if (state === 'STOPPING') return '停止中'
      if (state === 'STOPPED') return '已停止'
      if (state === 'ERROR') return '异常'
      return state
    },
    monitorStatusType(row) {
      const state = this.getMonitorState(row)
      if (state === 'RUNNING') return 'success'
      if (state === 'STARTING' || state === 'STOPPING') return 'warning'
      if (state === 'ERROR') return 'danger'
      return 'info'
    },
    returnType(type) {
      if (!type) return 'IPC'
      return String(type).toUpperCase()
    },
    async getList() {
      this.loading = true
      try {
        const response = await getDeviceList(this.queryParams)
        this.deviceList = response.rows || []
        this.total = response.total || 0

        // 首次加载自动选路播放：优先取 url 中的 ape_id，其次取第一个运行中或可用的设备
        if (!this.autoPreviewInitialized && this.deviceList.length > 0) {
          this.autoPreviewInitialized = true
          const targetApeId = this.$route.query.ape_id || this.$route.query.deviceId
          let target = null
          if (targetApeId) {
            target = this.deviceList.find(d => this.getApeId(d) === targetApeId)
          }
          if (!target) {
            target = this.deviceList.find(d => this.getMonitorState(d) === 'RUNNING') || this.deviceList[0]
          }
          if (target) {
            this.$nextTick(() => {
              this.playDeviceInActiveSlot(target)
            })
          }
        }
      } finally {
        this.loading = false
      }
    },
    handleQuery() {
      this.queryParams.pageNum = 1
      this.getList()
    },
    resetQuery() {
      this.resetForm('queryForm')
      this.queryParams.monitor_status = ''
      this.handleQuery()
    },
    async handleStart(row) {
      const apeId = this.getApeId(row)
      if (!apeId) return
      try {
        const response = await startDeviceMonitor(apeId)
        const payload = response && response.data && typeof response.data === 'object' ? response.data : (response || {})
        const hasSuccess = Object.prototype.hasOwnProperty.call(payload, 'success')
        this.$message({
          type: hasSuccess && !payload.success ? 'warning' : 'success',
          message: payload.shortMessage || '启动监控指令已发送'
        })
      } catch (err) {
        this.$modal.msgWarning('启动监控失败')
      } finally {
        await this.getList()
      }
    },
    async handleStop(row) {
      const apeId = this.getApeId(row)
      if (!apeId) return
      try {
        const response = await stopDeviceMonitor(apeId)
        const payload = response && response.data && typeof response.data === 'object' ? response.data : (response || {})
        const hasSuccess = Object.prototype.hasOwnProperty.call(payload, 'success')
        this.$message({
          type: hasSuccess && !payload.success ? 'warning' : 'success',
          message: payload.shortMessage || '停止监控指令已发送'
        })
      } catch (err) {
        this.$modal.msgWarning('停止监控失败')
      } finally {
        await this.getList()
      }
    },
    async handleAddToWall(row) {
      const sourceId = this.getApeId(row)
      if (!sourceId) {
        this.$modal.msgError('设备编码不存在，无法加入监控墙')
        return
      }
      try {
        const previewRes = await previewDeviceMonitor(sourceId)
        const playUrl = extractPlayableUrl(previewRes)
        if (!playUrl) {
          this.$modal.msgWarning('暂无可用播放流，无法加入监控墙')
          return
        }
        await upsertScreenWallStream({
          wallCode: 'main',
          sourceType: 'realtime',
          sourceId,
          deviceId: sourceId,
          playUrl,
          title: row.name || sourceId,
          slotIndex: null,
          enabled: true,
          taskPushEnabled: false,
          algorithmStreamUrl: ''
        })
        this.$modal.msgSuccess('已成功加入首页大屏监控墙')
      } catch (err) {
        this.$modal.msgError('加入监控墙失败')
      }
    }
  }
}
</script>

<style scoped>
.realtime-preview-container {
  display: flex;
  flex-direction: column;
  height: calc(100vh - 84px);
  padding: 16px;
  background-color: #0f1423;
  color: #e2e8f0;
  box-sizing: border-box;
}

.preview-header-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 18px;
  background-color: #1a2236;
  border-radius: 8px;
  margin-bottom: 14px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.25);
}

.header-left {
  display: flex;
  align-items: center;
}

.header-title {
  font-size: 17px;
  font-weight: 600;
  color: #38bdf8;
  display: flex;
  align-items: center;
  gap: 8px;
}

.header-right {
  display: flex;
  align-items: center;
}

.control-label {
  font-size: 13px;
  color: #94a3b8;
  margin-right: 8px;
}

.split-workbench {
  display: flex;
  flex: 1;
  gap: 14px;
  min-height: 0;
}

/* 左侧设备选择侧栏 */
.device-sidebar {
  width: 300px;
  background-color: #1a2236;
  border-radius: 8px;
  display: flex;
  flex-direction: column;
  padding: 14px;
  box-sizing: border-box;
}

.sidebar-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.sidebar-title {
  font-size: 14px;
  font-weight: 600;
  color: #cbd5e1;
}

.sidebar-search, .sidebar-filter {
  margin-bottom: 10px;
}

.device-tree-list {
  flex: 1;
  overflow-y: auto;
  padding-right: 4px;
}

.device-item-card {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 12px;
  background-color: #242f49;
  border: 1px solid #334155;
  border-radius: 6px;
  margin-bottom: 8px;
  cursor: pointer;
  transition: all 0.2s ease;
}

.device-item-card:hover {
  background-color: #2e3c5d;
  border-color: #38bdf8;
  transform: translateY(-1px);
}

.device-item-card.is-active {
  border-color: #38bdf8;
  background-color: rgba(56, 189, 248, 0.15);
}

.device-item-main {
  flex: 1;
  min-width: 0;
}

.device-name-row {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 4px;
}

.device-icon {
  color: #38bdf8;
  font-size: 14px;
}

.device-name {
  font-size: 13px;
  font-weight: 500;
  color: #f1f5f9;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.online-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}

.online-dot.is-online {
  background-color: #22c55e;
  box-shadow: 0 0 6px #22c55e;
}

.online-dot.is-offline {
  background-color: #64748b;
}

.device-sub-row {
  display: flex;
  align-items: center;
  gap: 8px;
}

.device-code {
  font-size: 11px;
  color: #94a3b8;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.device-item-actions {
  display: flex;
  gap: 4px;
  margin-left: 8px;
}

.empty-device-hint {
  text-align: center;
  padding: 40px 10px;
  color: #64748b;
}

.empty-device-hint i {
  font-size: 36px;
  margin-bottom: 8px;
}

.hint-sub {
  font-size: 12px;
  color: #475569;
}

/* 右侧多宫格视口 */
.grid-viewport {
  flex: 1;
  background-color: #0b0f19;
  border-radius: 8px;
  padding: 10px;
  box-sizing: border-box;
  display: flex;
  min-height: 0;
}

.slots-grid {
  display: grid;
  width: 100%;
  height: 100%;
  gap: 10px;
}

.grid-split-1 {
  grid-template-columns: 1fr;
  grid-template-rows: 1fr;
}

.grid-split-4 {
  grid-template-columns: repeat(2, 1fr);
  grid-template-rows: repeat(2, 1fr);
}

.grid-split-9 {
  grid-template-columns: repeat(3, 1fr);
  grid-template-rows: repeat(3, 1fr);
}

.slot-wrapper {
  background-color: #141b2d;
  border: 2px solid #232d44;
  border-radius: 6px;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  position: relative;
  transition: border-color 0.2s;
  cursor: pointer;
}

.slot-wrapper:hover {
  border-color: #38bdf8;
}

.slot-wrapper.is-selected {
  border-color: #0ea5e9;
  box-shadow: 0 0 10px rgba(14, 165, 233, 0.4);
}

.slot-header {
  height: 28px;
  background-color: rgba(15, 23, 42, 0.85);
  display: flex;
  align-items: center;
  padding: 0 10px;
  font-size: 12px;
  color: #94a3b8;
  gap: 8px;
  border-bottom: 1px solid #1e293b;
}

.slot-index-badge {
  color: #38bdf8;
  font-weight: 600;
}

.slot-title {
  flex: 1;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  color: #cbd5e1;
}

.slot-actions {
  display: flex;
  gap: 8px;
}

.slot-btn {
  cursor: pointer;
  color: #94a3b8;
  font-size: 14px;
}

.slot-btn:hover {
  color: #38bdf8;
}

.slot-video-body {
  flex: 1;
  position: relative;
  background-color: #000;
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 0;
}

.slot-video-element {
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.slot-overlay {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  color: #94a3b8;
  font-size: 13px;
  pointer-events: none;
}

.loading-overlay {
  background-color: rgba(0, 0, 0, 0.7);
  color: #38bdf8;
}

.loading-overlay i {
  font-size: 28px;
  margin-bottom: 8px;
}

.error-overlay {
  background-color: rgba(15, 23, 42, 0.85);
  color: #f87171;
  pointer-events: auto;
}

.error-overlay i {
  font-size: 32px;
  margin-bottom: 6px;
}

.idle-overlay {
  background-color: #101626;
}

.idle-icon {
  font-size: 38px;
  color: #334155;
  margin-bottom: 8px;
}

.idle-text {
  font-size: 13px;
  color: #64748b;
  font-weight: 500;
}

.idle-sub {
  font-size: 11px;
  color: #475569;
  margin-top: 4px;
}

/* 表格视图 */
.table-workbench {
  flex: 1;
  background-color: #1a2236;
  border-radius: 8px;
  padding: 16px;
  overflow-y: auto;
}
</style>
