<template>
  <div class="app-container">
    <div v-show="deviceListShow">
    <el-form :model="queryParams" ref="queryForm" size="small" :inline="true" label-width="68px">
      <el-form-item label="组织名称" prop="org_index">
        <el-select
          v-model="selectedQueryOrgIndex"
          filterable
          clearable
          placeholder="请选择组织名称"
          style="width: 240px"
          @change="handleQueryOrgChange"
        >
          <el-option
            v-for="item in queryDeptOptions"
            :key="item.value"
            :label="item.label"
            :value="item.value"
          />
        </el-select>
      </el-form-item>
      <el-form-item label="设备编码" prop="ape_id">
        <el-input
          v-model="queryParams.ape_id"
          placeholder="请输入设备编码"
          clearable
          style="width: 240px"
          @keyup.enter.native="handleQuery"
        />
      </el-form-item>
      <el-form-item label="设备名称" prop="name">
        <el-input
          v-model="queryParams.name"
          placeholder="请输入设备名称"
          clearable
          style="width: 240px"
          @keyup.enter.native="handleQuery"
        />
      </el-form-item>
      <el-form-item>
        <el-button type="primary" icon="el-icon-search" size="mini" @click="handleQuery">搜索</el-button>
        <el-button icon="el-icon-refresh" size="mini" @click="resetQuery">重置</el-button>
      </el-form-item>
    </el-form>

    <el-row :gutter="10" class="mb8">
      <el-col :span="1.5">
        <el-button
          type="primary"
          plain
          icon="el-icon-plus"
          size="mini"
          @click="handleAdd"
          v-hasPermi="['waring:device:add']"
        >新增</el-button>
      </el-col>
      <el-col :span="1.5">
        <el-button
          type="success"
          plain
          icon="el-icon-edit"
          size="mini"
          :disabled="single"
          @click="handleUpdate"
          v-hasPermi="['waring:device:edit']"
        >修改</el-button>
      </el-col>
      <el-col :span="1.5">
        <el-button
          type="danger"
          plain
          icon="el-icon-delete"
          size="mini"
          :disabled="multiple"
          @click="handleDelete"
          v-hasPermi="['waring:device:remove']"
        >删除</el-button>
      </el-col>
      <el-col :span="1.5">
        <el-button
          type="warning"
          plain
          icon="el-icon-video-camera"
          size="mini"
          @click="goToRealtime()"
        >实时监控中心</el-button>
      </el-col>
      <el-col :span="1.5">
        <el-button
          type="info"
          plain
          icon="el-icon-refresh"
          size="mini"
          @click="syncGbDevices"
        >同步国标设备</el-button>
      </el-col>
    </el-row>

    <el-table v-loading="loading" :data="deviceList" @selection-change="handleSelectionChange">
      <el-table-column type="selection" width="50" align="center" />
      <el-table-column label="设备编码" prop="ape_id" align="center" :show-overflow-tooltip="true" />
      <el-table-column label="设备名称" prop="name" align="center" :show-overflow-tooltip="true" />
      <el-table-column label="协议类型" prop="device_type" align="center">
        <template slot-scope="scope">
          <el-tag size="mini" :type="String(scope.row.device_type).toLowerCase() === 'gb28181' ? 'success' : 'info'">
            {{ formatDeviceType(scope.row.device_type) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="流来源" prop="stream_source_type" align="center">
        <template slot-scope="scope">
          <el-tag size="mini" :type="scope.row.stream_source_type === 'PLATFORM' ? 'success' : 'info'">
            {{ formatSourceType(scope.row.stream_source_type) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="视频流地址" prop="direct_source_url" align="center" :show-overflow-tooltip="true" />
      <el-table-column label="IP地址" prop="ip_addr" align="center" />
      <el-table-column label="端口" prop="port" align="center" />
      <el-table-column label="组织编码" prop="org_index" align="center" :show-overflow-tooltip="true" />
      <el-table-column label="组织名称" prop="org_name" align="center" :show-overflow-tooltip="true" />
      <el-table-column label="位置" prop="place" align="center" :show-overflow-tooltip="true" />
      <el-table-column label="在线状态" prop="is_online" align="center" width="100">
        <template slot-scope="scope">
          <el-tag size="mini" :type="String(scope.row.is_online) === '1' ? 'success' : 'info'">
            {{ formatOnline(scope.row.is_online) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="监控状态" prop="monitor_status" align="center" width="100">
        <template slot-scope="scope">
          <el-tag size="mini" :type="monitorStatusType(scope.row.monitor_status)">
            {{ formatMonitorStatus(scope.row.monitor_status) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="操作" align="center" fixed="right" class-name="small-padding fixed-width operation-column" width="460">
        <template slot-scope="scope">
          <el-button
            size="mini"
            type="text"
            icon="el-icon-video-play"
            @click="startMonitor(scope.row)"
            v-hasPermi="['waring:device:start']"
          >启动监控</el-button>
          <el-button
            size="mini"
            type="text"
            icon="el-icon-video-pause"
            @click="stopMonitor(scope.row)"
            v-hasPermi="['waring:device:stop']"
          >停止监控</el-button>
          <el-button
            size="mini"
            type="text"
            icon="el-icon-monitor"
            style="color: #e6a23c;"
            @click="goToRealtime(scope.row)"
          >进入监控</el-button>
          <el-button
            size="mini"
            type="text"
            icon="el-icon-video-camera"
            @click="handlePreview(scope.row)"
            v-hasPermi="['waring:device:query']"
          >预览视频</el-button>
          <el-button
            size="mini"
            type="text"
            icon="el-icon-zoom-in"
            @click="warningHistory(scope.row)"
            v-hasPermi="['waring:device:history']"
          >历史报警</el-button>
          <el-button
            size="mini"
            type="text"
            icon="el-icon-edit"
            @click="handleUpdate(scope.row)"
            v-hasPermi="['waring:device:edit']"
          >修改</el-button>
          <el-button
            size="mini"
            type="text"
            icon="el-icon-delete"
            @click="handleDelete(scope.row)"
            v-hasPermi="['waring:device:remove']"
          >删除</el-button>
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

    <devicewarning
      v-show="!deviceListShow"
      @closeWarning="deviceListShow = true"
      :warningTitle="warningTitle"
      :device_id="device_id"
    />

    <player
      v-show="viewProof"
      :viewProof="viewProof"
      :rtspUrl="rtspUrl"
      title="实时监控预览"
      @closeProof="viewProof = false"
    />

    <el-dialog :title="title" :visible.sync="open" width="620px" append-to-body>
      <el-form ref="form" :model="form" :rules="rules" label-width="90px">
        <el-row>
          <el-col :span="12">
            <el-form-item label="流来源" prop="stream_source_type">
              <el-select v-model="form.stream_source_type" placeholder="请选择流来源" style="width: 100%" @change="handleSourceTypeChange">
                <el-option v-for="item in streamSourceTypeOptions" :key="item.value" :label="item.label" :value="item.value" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="设备编码" prop="ape_id">
              <el-input v-model="form.ape_id" :placeholder="form.stream_source_type === 'DIRECT' ? 'DIRECT 默认自动生成，可手动修改' : '请输入设备编码'">
                <el-button
                  v-if="form.stream_source_type === 'DIRECT' && !isEdit"
                  slot="append"
                  icon="el-icon-refresh"
                  @click="refreshApeId"
                >刷新</el-button>
              </el-input>
            </el-form-item>
          </el-col>
        </el-row>
        <el-row>
          <el-col :span="12">
            <el-form-item label="设备名称" prop="name">
              <el-input v-model="form.name" placeholder="请输入设备名称" />
            </el-form-item>
          </el-col>
          <el-col :span="12" v-if="form.stream_source_type === 'DIRECT'">
            <el-form-item label="视频流地址" prop="direct_source_url">
              <el-input v-model="form.direct_source_url" placeholder="请输入视频流地址" />
            </el-form-item>
          </el-col>
        </el-row>
        <el-row>
          <el-col :span="12">
            <el-form-item label="组织名称" prop="org_name" :required="!isEdit">
              <el-select
                v-model="selectedOrgIndex"
                filterable
                clearable
                placeholder="请选择组织名称"
                style="width: 100%"
                @change="handleFormOrgChange"
              >
                <el-option
                  v-for="item in queryDeptOptions"
                  :key="item.value"
                  :label="item.label"
                  :value="item.value"
                />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="IP地址" prop="ip_addr" v-if="form.stream_source_type === 'PLATFORM'">
              <el-input v-model="form.ip_addr" placeholder="请输入IP地址" />
            </el-form-item>
            <el-form-item label="IP地址" prop="ip_addr" v-else>
              <el-input v-model="form.ip_addr" placeholder="可选" />
            </el-form-item>
          </el-col>
        </el-row>
        <el-row>
          <el-col :span="12">
            <el-form-item label="端口" prop="port" v-if="form.stream_source_type === 'PLATFORM'">
              <el-input v-model="form.port" placeholder="请输入端口" />
            </el-form-item>
            <el-form-item label="端口" prop="port" v-else>
              <el-input v-model="form.port" placeholder="可选" />
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="位置" prop="place" v-if="form.stream_source_type === 'PLATFORM'">
              <el-input v-model="form.place" placeholder="请输入位置" />
            </el-form-item>
            <el-form-item label="位置" prop="place" v-else>
              <el-input v-model="form.place" placeholder="可选" />
            </el-form-item>
          </el-col>
        </el-row>
        <el-row>
          <el-col :span="12">
            <el-form-item label="在线状态" prop="is_online">
              <el-select v-model="form.is_online" placeholder="请选择在线状态" style="width: 100%">
                <el-option v-for="item in onlineOptions" :key="item.value" :label="item.label" :value="item.value" />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>
      </el-form>
      <div slot="footer" class="dialog-footer">
        <el-button type="primary" @click="submitForm">确 定</el-button>
        <el-button @click="cancel">取 消</el-button>
      </div>
    </el-dialog>
  </div>
</template>

<script>
import { getDeviceList, getDevice, addDevice, updateDevice, delDevice, startDeviceMonitor, stopDeviceMonitor, previewDeviceMonitor, syncGbDevices as syncGbDevicesApi } from '@/api/device'
import { deptTreeSelect } from '@/api/system/user'
import player from '@/components/RTSPPlayer'
import devicewarning from './components/device-warning.vue'
import { extractPlayableUrl } from '@/utils/mediaPlayback'

export default {
  name: 'DeviceManage',
  components: { devicewarning, player },
  data() {
    const validateDirectSourceUrl = (rule, value, callback) => {
      if (this.form.stream_source_type === 'DIRECT' && !value) {
        callback(new Error('DIRECT 流来源下，视频流地址不能为空'))
        return
      }
      callback()
    }
    const validateOrgName = (rule, value, callback) => {
      if (!this.isEdit && !value) {
        callback(new Error('组织名称不能为空'))
        return
      }
      callback()
    }
    return {
      loading: false,
      total: 0,
      ids: [],
      single: true,
      multiple: true,
      open: false,
      title: '',
      warningTitle: '',
      device_id: '',
      deviceListShow: true,
      viewProof: false,
      rtspUrl: '',
      isEdit: false,
      deptOptions: [],
      queryDeptOptions: [],
      selectedOrgIndex: undefined,
      selectedQueryOrgIndex: undefined,
      deviceList: [],
      streamSourceTypeOptions: [
        { value: 'DIRECT', label: '直连' },
        { value: 'PLATFORM', label: '平台' }
      ],
      onlineOptions: [
        { value: '0', label: '登录中' },
        { value: '1', label: '在线/启用' },
        { value: '2', label: '离线/停用' },
        { value: '9', label: '其他/异常' }
      ],
      queryParams: {
        pageNum: 1,
        pageSize: 10,
        ape_id: undefined,
        name: undefined,
        org_index: undefined
      },
      form: {},
      rules: {
        name: [{ required: true, message: '设备名称不能为空', trigger: 'blur' }],
        org_name: [{ validator: validateOrgName, trigger: 'change' }],
        direct_source_url: [{ validator: validateDirectSourceUrl, trigger: ['blur', 'change'] }]
      }
    }
  },
  created() {
    this.getDeptTree()
    this.getList()
  },
  methods: {
    getDeptTree() {
      deptTreeSelect().then((response) => {
        this.deptOptions = response.data || []
        this.queryDeptOptions = this.buildQueryDeptOptions(this.deptOptions)
      })
    },
    buildQueryDeptOptions(nodes, parentLabel = '') {
      if (!Array.isArray(nodes) || nodes.length === 0) {
        return []
      }
      const options = []
      nodes.forEach((node) => {
        const value = node.org_index !== undefined && node.org_index !== null && node.org_index !== ''
          ? node.org_index
          : node.id
        const orgIndex = node.org_index !== undefined && node.org_index !== null && node.org_index !== ''
          ? node.org_index
          : value
        const currentLabel = node.label || node.deptName || node.org_name || ''
        const label = parentLabel && currentLabel ? `${parentLabel} / ${currentLabel}` : currentLabel

        if (value !== undefined && value !== null && value !== '' && label) {
          options.push({ value, label, orgIndex, orgName: currentLabel })
        }

        if (Array.isArray(node.children) && node.children.length > 0) {
          options.push(...this.buildQueryDeptOptions(node.children, label || parentLabel))
        }
      })
      return options
    },
    ensureFormOrgOption(orgIndex, orgName) {
      if (orgIndex === null || orgIndex === undefined || orgIndex === '') {
        return undefined
      }
      const normalizedOrgIndex = String(orgIndex)
      const matchedOption = this.queryDeptOptions.find((item) => String(item.orgIndex) === normalizedOrgIndex)
      if (matchedOption) {
        return matchedOption.value
      }
      const normalizedOrgName = orgName || ''
      let optionLabel = normalizedOrgIndex
      if (normalizedOrgName) {
        if (normalizedOrgName.includes('/')) {
          optionLabel = normalizedOrgName
        } else {
          const sameNameOption = this.queryDeptOptions.find((item) => item.orgName === normalizedOrgName)
          optionLabel = sameNameOption && sameNameOption.label ? sameNameOption.label : normalizedOrgName
        }
      }
      const tempOption = {
        value: normalizedOrgIndex,
        label: optionLabel,
        orgIndex: normalizedOrgIndex,
        orgName: normalizedOrgName
      }
      this.queryDeptOptions.push(tempOption)
      return tempOption.value
    },
    handleFormOrgChange(value) {
      if (value === null || value === undefined || value === '') {
        this.form.org_index = undefined
        this.form.org_name = undefined
        return
      }
      const option = this.queryDeptOptions.find((item) => String(item.value) === String(value))
      if (option) {
        this.form.org_index = option.orgIndex !== undefined && option.orgIndex !== null && option.orgIndex !== ''
          ? option.orgIndex
          : value
        this.form.org_name = option.orgName
        return
      }
      this.form.org_index = value
      this.form.org_name = undefined
    },
    handleQueryOrgChange(value) {
      this.queryParams.org_index = value === null || value === undefined || value === '' ? undefined : value
    },
    formatDeviceType(value) {
      return String(value || '').toLowerCase() === 'gb28181' ? 'GB28181' : 'RTSP'
    },
    formatSourceType(value) {
      if (String(value).toUpperCase() === 'PLATFORM') {
        return '平台'
      }
      if (String(value).toUpperCase() === 'DIRECT') {
        return '直连'
      }
      return value || '直连'
    },
    formatOnline(value) {
      return String(value) === '1' ? '在线' : '离线'
    },
    formatMonitorStatus(value) {
      const labels = {
        RUNNING: '运行中',
        STOPPED: '已停止',
        STARTING: '启动中',
        STOPPING: '停止中',
        ERROR: '异常'
      }
      const status = String(value || '').toUpperCase()
      return labels[status] || status || '未知'
    },
    monitorStatusType(value) {
      const status = String(value || '').toUpperCase()
      if (status === 'RUNNING') return 'success'
      if (status === 'ERROR') return 'danger'
      if (status === 'STARTING' || status === 'STOPPING') return 'warning'
      return 'info'
    },
    generateApeId() {
      const randomPart = Math.floor(100000 + Math.random() * 900000)
      return `cam${randomPart}`
    },
    refreshApeId() {
      this.form.ape_id = this.generateApeId()
    },
    handleSourceTypeChange(value) {
      const normalized = String(value || 'DIRECT').toUpperCase()
      this.form.stream_source_type = normalized
      if (normalized === 'DIRECT') {
        if (!this.isEdit && !this.form.ape_id) {
          this.form.ape_id = this.generateApeId()
        }
      } else {
        if (!this.isEdit) {
          this.form.ape_id = undefined
        }
        this.form.direct_source_url = undefined
      }
      this.$nextTick(() => {
        if (this.$refs.form) {
          this.$refs.form.clearValidate(['direct_source_url'])
        }
      })
    },
    getList() {
      this.loading = true
      getDeviceList(this.queryParams).then((response) => {
        this.deviceList = response.rows || []
        this.total = response.total || 0
        this.loading = false
      }).catch(() => {
        this.loading = false
      })
    },
    async syncGbDevices() {
      try {
        const response = await syncGbDevicesApi()
        this.$modal.msgSuccess((response && response.msg) || '国标设备同步完成')
        this.getList()
      } catch (error) {
        this.$modal.msgError((error && error.message) || '国标设备同步失败，请稍后重试')
      }
    },
    cancel() {
      this.open = false
      this.reset()
    },
    reset() {
      this.form = {
        ape_id: undefined,
        name: undefined,
        device_type: 'rtsp',
        stream_source_type: 'DIRECT',
        direct_source_url: undefined,
        ip_addr: undefined,
        port: undefined,
        org_index: undefined,
        org_name: undefined,
        place: undefined,
        is_online: undefined
      }
      this.selectedOrgIndex = undefined
      this.viewProof = false
      this.rtspUrl = ''
      this.isEdit = false
      this.resetForm('form')
    },
    handleQuery() {
      this.queryParams.pageNum = 1
      this.getList()
    },
    resetQuery() {
      this.resetForm('queryForm')
      this.selectedQueryOrgIndex = undefined
      this.queryParams.org_index = undefined
      this.handleQuery()
    },
    handleSelectionChange(selection) {
      this.ids = selection.map((item) => item.ape_id)
      this.single = selection.length !== 1
      this.multiple = selection.length === 0
    },
    handleAdd() {
      this.reset()
      if (this.form.stream_source_type === 'DIRECT') {
        this.form.ape_id = this.generateApeId()
      }
      this.open = true
      this.title = '新增设备'
    },
    handleUpdate(row) {
      this.reset()
      const apeId = row.ape_id || this.ids[0]
      if (!apeId) {
        return
      }
      getDevice(apeId).then((response) => {
        this.form = Object.assign({}, this.form, response.data || {})
        this.form.device_type = this.form.device_type || 'rtsp'
        this.form.stream_source_type = (this.form.stream_source_type || 'DIRECT').toUpperCase()
        this.selectedOrgIndex = this.ensureFormOrgOption(this.form.org_index, this.form.org_name)
        this.handleFormOrgChange(this.selectedOrgIndex)
        this.open = true
        this.title = '修改设备'
        this.isEdit = true
      })
    },
    submitForm() {
      this.$refs.form.validate((valid) => {
        if (!valid) {
          return
        }
        this.form.stream_source_type = (this.form.stream_source_type || 'DIRECT').toUpperCase()
        this.form.device_type = this.form.device_type || 'rtsp'
        const request = this.isEdit ? updateDevice(this.form) : addDevice(this.form)
        request.then(() => {
          this.$modal.msgSuccess(this.isEdit ? '修改成功' : '新增成功')
          this.open = false
          this.getList()
        })
      })
    },
    handleDelete(row) {
      const apeIds = row.ape_id || this.ids
      if (!apeIds || (Array.isArray(apeIds) && apeIds.length === 0)) {
        return
      }
      this.$modal.confirm('是否确认删除设备编号为"' + apeIds + '"的数据项？').then(() => {
        return delDevice(apeIds)
      }).then(() => {
        this.getList()
        this.$modal.msgSuccess('删除成功')
      }).catch(() => {})
    },
    goToRealtime(row) {
      const query = {}
      if (row) {
        const apeId = row.ape_id || row.apeId || row.deviceId || row.device_id
        if (apeId) {
          query.ape_id = apeId
        }
      }
      this.$router.push({ path: '/video/realtime', query })
    },
    async startMonitor(row) {
      try {
        const response = await startDeviceMonitor(row.ape_id)
        const payload = response && response.data && typeof response.data === 'object' ? response.data : {}
        const shortMessage = payload.shortMessage || '已启动监控，可点击“进入监控”或前往“实时监控”查看画面。'
        const hasSuccess = Object.prototype.hasOwnProperty.call(payload, 'success')
        this.$message({
          type: hasSuccess && !payload.success ? 'warning' : 'success',
          message: shortMessage
        })
        this.getList()
      } catch (error) {
        this.$modal.msgError((error && error.message) || '启动监控失败，请稍后重试')
      }
    },
    async stopMonitor(row) {
      try {
        const response = await stopDeviceMonitor(row.ape_id)
        const payload = response && response.data && typeof response.data === 'object' ? response.data : {}
        const hasSuccess = Object.prototype.hasOwnProperty.call(payload, 'success')
        const isFailed = hasSuccess && !payload.success
        const shortMessage = payload.shortMessage || (isFailed ? '停止监控失败，请稍后重试' : '已停止监控。')
        this.$message({
          type: isFailed ? 'warning' : 'success',
          message: shortMessage
        })
        this.getList()
      } catch (error) {
        this.$modal.msgError((error && error.message) || '停止监控失败，请稍后重试')
      }
    },
    async handlePreview(row) {
      const apeId = row.ape_id || row.apeId || row.device_id || row.deviceId
      if (!apeId) {
        this.$modal.msgError('设备编码不存在，无法预览')
        return
      }
      try {
        const response = await previewDeviceMonitor(apeId)
        const playUrl = extractPlayableUrl(response)
        if (!playUrl) {
          this.$modal.msgWarning('暂无可用播放流')
          return
        }
        this.rtspUrl = playUrl
        this.viewProof = true
      } catch (e) {
        this.$modal.msgError('获取预览流地址失败，请稍后重试')
      }
    },
    warningHistory(row) {
      this.device_id = row.ape_id || row.apeId || row.place
      this.deviceListShow = false
      this.warningTitle = `正在查看「${row.name}」的历史报警信息`
    }
  }
}
</script>

<style scoped>
::v-deep .operation-column .cell {
  white-space: nowrap;
}

::v-deep .operation-column .el-button + .el-button {
  margin-left: 6px;
}

::v-deep .operation-column .el-button--text {
  padding: 0;
}
</style>
