const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const vm = require('node:vm')
const path = require('node:path')

// 直接执行组件脚本的方法，覆盖请求返回后的状态变化，无需启动后端。
function component(getConfig, updateConfig = async () => ({})) {
  const file = path.join(__dirname, '../src/views/algorithm/components/AlgorithmConfigModal.vue')
  const source = fs.readFileSync(file, 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1]
    .replace(/^import .*$/m, '')
    .replace('export default', 'module.exports =')
  const context = { module: { exports: {} }, getAlgorithmTuningConfig: getConfig, updateAlgorithmTuning: updateConfig }
  vm.runInNewContext(source, context)
  const options = context.module.exports
  const messages = []
  const instance = { ...options.data(), $modal: { msgSuccess: msg => messages.push(msg), msgError: msg => messages.push(msg) }, $emit() {} }
  Object.entries(options.methods).forEach(([key, value]) => { instance[key] = value.bind(instance) })
  return { instance, messages }
}

test('重新打开时只回填编辑默认值，不展示历史请求为当前配置', async () => {
  const { instance } = component(async () => ({ data: {
    defaultConfig: { preset: 'MEDIUM', confirmWindowSec: 15 },
    current: { preset: 'HIGH', confirmWindowSec: 3 },
    lastApplied: { preset: 'HIGH', updatedControls: ['task-a'] }
  } }))
  await instance.loadConfig()
  assert.equal(instance.preset, 'MEDIUM')
  assert.equal(instance.form.confirmWindowSec, 15)
  assert.equal(instance.lastApplied.preset, 'HIGH')
  assert.equal(instance.configLoaded, true)
})

test('配置读取失败清除旧状态并禁止提交', async () => {
  let calls = 0
  const { instance } = component(async () => { throw Error('offline') }, async () => { calls++ })
  instance.configLoaded = true
  instance.lastApplied = { preset: 'HIGH' }
  await instance.loadConfig()
  await instance.apply()
  assert.equal(instance.lastApplied, null)
  assert.equal(instance.configLoaded, false)
  assert.equal(calls, 0)
})

test('提交明确广播范围并展示后端实际任务回执', async () => {
  let payload
  const receipt = { preset: 'MEDIUM', updatedControls: ['task-a', 'task-b'], globalThresholdsUpdated: true }
  const { instance, messages } = component(async () => ({ data: {} }), async value => {
    payload = value
    return { msg: '本次已更新 2 个运行中的睡岗布控', lastApplied: receipt }
  })
  await instance.loadConfig()
  await instance.apply()
  assert.equal(payload.controlCode, '*')
  assert.equal(instance.lastApplied, receipt)
  assert.equal(messages[0], '本次已更新 2 个运行中的睡岗布控')
  assert.equal(instance.saving, false)
})

test('更新失败不制造成功回执', async () => {
  const { instance, messages } = component(async () => ({ data: {} }), async () => { throw Error('没有运行布控') })
  await instance.loadConfig()
  await instance.apply()
  assert.equal(instance.lastApplied, null)
  assert.equal(messages[0], '没有运行布控')
  assert.equal(instance.saving, false)
})
