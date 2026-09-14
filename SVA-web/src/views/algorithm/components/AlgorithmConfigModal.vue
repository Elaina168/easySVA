<template>
  <el-dialog
    title="算法灵敏度动态调参（热加载）"
    :visible.sync="dialogVisible"
    width="600px"
    :close-on-click-modal="false"
    append-to-body
    custom-class="algo-tuning-dialog"
    @open="handleOpen"
  >
    <div v-loading="loading" class="tuning-wrap">
      <el-alert
        type="info"
        :closable="false"
        show-icon
        title="本页修改全部运行中的睡岗布控；目标检出置信度和 NMS 作用于共享 Pose 模型，并非仅修改当前选中的摄像头。"
        style="margin-bottom: 14px;"
      />

      <el-alert
        type="warning"
        :closable="false"
        title="表单为待提交参数，不是引擎实时配置。热调不保存到布控配置；重启分析引擎或重新启动布控后请重新应用。后端重启仅清除下发记录，不会重置仍在运行的引擎。"
        style="margin-bottom: 14px;"
      />
      <!-- 预设档位 -->
      <div class="preset-title">选择灵敏度档位</div>
      <el-radio-group :disabled="saving || loading" v-model="preset" class="preset-group" @input="onPresetChange">
        <el-radio
          v-for="p in presets"
          :key="p.key"
          :label="p.key"
          class="preset-radio"
          border
        >
          <el-tag :type="p.tagType" size="mini" effect="dark" style="margin-right:6px;">{{ p.label }}</el-tag>
          <span class="preset-desc">{{ p.desc }}</span>
        </el-radio>
      </el-radio-group>

      <!-- 专家自定义滑块 -->
      <div v-show="preset === 'CUSTOM'" class="expert-panel">
        <div class="preset-title" style="margin-top:14px;">
          专家参数微调
          <span class="expert-hint">（仅专家档可逐项调节）</span>
        </div>
        <div v-for="field in expertFields" :key="field" class="slider-row">
          <div class="slider-label">
            <span>{{ rangeOf(field).label }}</span>
            <el-tooltip :content="rangeOf(field).tip" placement="top">
              <i class="el-icon-question slider-tip"></i>
            </el-tooltip>
            <span class="slider-value">{{ formatVal(form[field]) }}{{ field === 'confirmWindowSec' ? ' s' : (field === 'recoveryMs' || field === 'motionWindowMs') ? ' ms' : (field === 'torsoAngleDegMin' || field === 'shoulderTiltDegMin') ? '°' : '' }}</span>
          </div>
          <el-slider
            v-model="form[field]"
            :disabled="saving || loading"
            :min="rangeOf(field).min"
            :max="rangeOf(field).max"
            :step="rangeOf(field).step"
            :show-tooltip="false"
            style="flex:1;margin-left:12px;"
          />
        </div>
      </div>

      <div class="current-line" v-if="lastApplied">
        最近一次下发记录（不代表当前实时状态）：
        <el-tag size="mini" type="info">{{ presetLabel(lastApplied.preset) }}</el-tag>
        <div>时间：{{ lastApplied.appliedAt }}</div>
        <div>当时更新的布控：{{ (lastApplied.updatedControls || []).join('、') }}</div>
        <div>{{ lastApplied.globalThresholdsUpdated ? '本次同时修改了共享 Pose 模型阈值' : '本次未修改共享模型阈值' }}</div>
      </div>
    </div>

    <div slot="footer">
      <el-button @click="dialogVisible = false">取 消</el-button>
      <el-button type="primary" :loading="saving" :disabled="loading || !configLoaded" icon="el-icon-lightning" @click="apply">
        立即热加载
      </el-button>
    </div>
  </el-dialog>
</template>

<script>
import { getAlgorithmTuningConfig, updateAlgorithmTuning } from '@/api/algorithmConfig'

// 三档预设的核心数值（与后端 AlgorithmTuningController.buildPreset 保持一致）
const PRESET_VALUES = {
  HIGH: {
    confirmWindowSec: 3,
    detectionConfidence: 0.25,
    sleepPositiveRatio: 0.7,
    headHeightRatioMax: 0.52
  },
  MEDIUM: {
    confirmWindowSec: 15,
    detectionConfidence: 0.35,
    sleepPositiveRatio: 0.8,
    headHeightRatioMax: 0.48
  },
  LOW: {
    confirmWindowSec: 30,
    detectionConfidence: 0.4,
    sleepPositiveRatio: 0.85,
    headHeightRatioMax: 0.42
  }
}

function defaultForm() {
  return {
    confirmWindowSec: 15,
    detectionConfidence: 0.35,
    nmsThreshold: 0.45,
    keypointConfidence: 0.35,
    sleepPositiveRatio: 0.8,
    headHeightRatioMax: 0.48,
    headArmDistanceRatioMax: 0.75,
    headMotionRatioMax: 0.15,
    recoveryMs: 2000,
    torsoAngleDegMin: 25,
    shoulderTiltDegMin: 15,
    motionWindowMs: 2000
  }
}

export default {
  name: 'AlgorithmConfigModal',
  props: {
    visible: { type: Boolean, default: false }
  },
  data() {
    return {
      loading: false,
      saving: false,
      preset: 'MEDIUM',
      lastApplied: null,
      configLoaded: false,
      form: defaultForm(),
      presets: [],
      ranges: {},
      expertFields: [
        'confirmWindowSec',
        'detectionConfidence',
        'sleepPositiveRatio',
        'headHeightRatioMax',
        'keypointConfidence',
        'nmsThreshold',
        'headArmDistanceRatioMax',
        'headMotionRatioMax',
        'torsoAngleDegMin',
        'shoulderTiltDegMin',
        'recoveryMs',
        'motionWindowMs'
      ]
    }
  },
  computed: {
    dialogVisible: {
      get() { return this.visible },
      set(val) { this.$emit('update:visible', val) }
    }
  },
  methods: {
    rangeOf(field) {
      return this.ranges[field] || { min: 0, max: 1, step: 0.01, label: field, tip: '' }
    },
    formatVal(v) {
      if (typeof v !== 'number') return v
      return Number.isInteger(v) ? v : Number(v.toFixed(2))
    },
    presetLabel(key) {
      const hit = this.presets.find(p => p.key === key)
      return hit ? hit.label : key
    },
    handleOpen() {
      this.loadConfig()
    },
    loadConfig() {
      this.loading = true
      this.configLoaded = false
      this.lastApplied = null
      this.preset = 'MEDIUM'
      this.form = defaultForm()
      return getAlgorithmTuningConfig().then(res => {
        const data = res.data || {}
        this.presets = data.presets || []
        this.ranges = data.ranges || {}
        // 只使用明确标识的编辑默认值，绝不将历史下发记录当作实时状态。
        const defaults = data.defaultConfig || {}
        if (defaults.preset) this.preset = defaults.preset
        Object.keys(this.form).forEach(k => {
          if (defaults[k] !== null && defaults[k] !== undefined) this.form[k] = defaults[k]
        })
        this.lastApplied = data.lastApplied || null
        this.configLoaded = true
      }).catch(() => {
        this.$modal.msgError('调参配置加载失败，请关闭后重试')
      }).finally(() => { this.loading = false })
    },
    onPresetChange(key) {
      const tpl = PRESET_VALUES[key]
      if (tpl) {
        // 选择固定档位时，把该档核心数值写入表单（提交时以后端模板为准）
        Object.assign(this.form, tpl)
      }
    },
    apply() {
      if (this.saving || this.loading || !this.configLoaded) return
      this.saving = true
      const payload = Object.assign({ controlCode: '*', preset: this.preset }, this.form)
      return updateAlgorithmTuning(payload).then(res => {
        this.lastApplied = res.lastApplied || null
        this.$modal.msgSuccess(res.msg)
        this.$emit('applied', this.lastApplied)
        this.dialogVisible = false
      }).catch(err => {
        this.$modal.msgError((err && err.message) || '热加载失败，请确认分析引擎已启动')
      }).finally(() => { this.saving = false })
    }
  }
}
</script>

<style scoped>
.tuning-wrap { max-height: 62vh; overflow-y: auto; padding-right: 4px; }
.preset-title { font-size: 13px; font-weight: 600; color: #303133; margin-bottom: 8px; }
.expert-hint { font-size: 12px; font-weight: 400; color: #909399; }
.preset-group { display: flex; flex-direction: column; width: 100%; }
/* 关键：Element 带 border 的相邻 radio 默认有 margin-left:10px，纵向排列时会让第2项起整体右移，这里强制左对齐 */
.preset-radio {
  width: 100%;
  height: auto;
  margin: 0 0 8px 0 !important;
  padding: 8px 10px;
  white-space: normal;
  box-sizing: border-box;
}
.preset-radio.is-bordered + .preset-radio.is-bordered { margin-left: 0 !important; }
.preset-desc { color: #606266; font-size: 12px; }
.expert-panel { border-top: 1px dashed #dcdfe6; padding-top: 4px; }
.slider-row { display: flex; align-items: center; margin-bottom: 6px; }
.slider-label { width: 200px; font-size: 12px; color: #606266; display: flex; align-items: center; }
.slider-tip { color: #c0c4cc; margin: 0 4px; cursor: help; }
.slider-value { margin-left: auto; color: #409eff; font-weight: 600; min-width: 52px; text-align: right; }
.current-line { margin-top: 10px; font-size: 12px; color: #909399; }
</style>
