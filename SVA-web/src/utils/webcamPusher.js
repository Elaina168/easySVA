/**
 * easySVA 全局电脑摄像头推流管理服务 (Singleton)
 * 作用: 全局常驻推流至 WebSocket 中继 (/webcam-ws/)，不受页面路由与标签页切换影响。
 * 供 实时监控、设备管理视频预览、布控管理 AI 算法实时分析等全系统共享。
 */

class WebcamPusher {
  constructor() {
    this.isActive = false
    this.mediaStream = null
    this.recorder = null
    this.socket = null
    this.heartbeatTimer = null
    this.listeners = new Set()
    this.stats = {
      chunkCount: 0,
      startTime: null
    }
  }

  subscribe(callback) {
    this.listeners.add(callback)
    callback(this.isActive)
    return () => this.listeners.delete(callback)
  }

  notify() {
    this.listeners.forEach(fn => {
      try { fn(this.isActive) } catch (e) { console.error(e) }
    })
  }

  async toggle() {
    if (this.isActive) {
      this.stop()
      return false
    } else {
      return await this.start()
    }
  }

  async start() {
    if (this.isActive) return true
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      throw new Error('当前浏览器环境不支持获取本地摄像头，请使用 Chrome 或 Edge 浏览器')
    }

    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: 1280 },
          height: { ideal: 720 },
          frameRate: { ideal: 25 }
        },
        audio: false
      })
      this.mediaStream = stream

      let mimeType = 'video/webm;codecs=vp8'
      if (typeof MediaRecorder !== 'undefined') {
        if (MediaRecorder.isTypeSupported('video/webm;codecs=vp8')) {
          mimeType = 'video/webm;codecs=vp8'
        } else if (MediaRecorder.isTypeSupported('video/webm')) {
          mimeType = 'video/webm'
        }
      }

      const wsProto = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
      const wsUrl = `${wsProto}//${window.location.host}/webcam-ws/`
      this.socket = new WebSocket(wsUrl)
      this.socket.binaryType = 'arraybuffer'

      await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('连接推流中继超时')), 8000)
        this.socket.onopen = () => {
          clearTimeout(timer)
          resolve()
        }
        this.socket.onerror = (e) => {
          clearTimeout(timer)
          reject(new Error('连接推流中继失败'))
        }
      })

      this.isActive = true
      this.stats.chunkCount = 0
      this.stats.startTime = Date.now()
      this.notify()

      this.recorder = new MediaRecorder(stream, {
        mimeType,
        videoBitsPerSecond: 1500000
      })

      this.recorder.ondataavailable = async (e) => {
        if (e.data && e.data.size > 0 && this.socket && this.socket.readyState === WebSocket.OPEN) {
          const buf = await e.data.arrayBuffer()
          this.socket.send(buf)
          this.stats.chunkCount++
        }
      }

      this.recorder.start(250) // 250ms 切片，降低 WebSocket 帧碎片并保持毫秒级极低延迟

      // 心跳保活
      this.heartbeatTimer = setInterval(() => {
        if (this.socket && this.socket.readyState === WebSocket.OPEN) {
          this.socket.send('ping')
        }
      }, 5000)

      this.socket.onclose = () => {
        if (this.isActive) {
          console.warn('[WebcamPusher] WebSocket 连接意外关闭，停止推流')
          this.stop()
        }
      }

      return true
    } catch (err) {
      this.stop()
      throw err
    }
  }

  stop() {
    this.isActive = false
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer)
      this.heartbeatTimer = null
    }
    if (this.recorder) {
      try {
        if (this.recorder.state !== 'inactive') {
          this.recorder.stop()
        }
      } catch (e) {}
      this.recorder = null
    }
    if (this.socket) {
      try {
        this.socket.close()
      } catch (e) {}
      this.socket = null
    }
    if (this.mediaStream) {
      try {
        this.mediaStream.getTracks().forEach(t => t.stop())
      } catch (e) {}
      this.mediaStream = null
    }
    this.notify()
  }
}

export default new WebcamPusher()
