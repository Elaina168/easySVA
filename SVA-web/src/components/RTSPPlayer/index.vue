<template>
  <el-card :class="['box-card', { 'box-card--inline': inline }]" :style="cardStyle">
    <div slot="header" class="clearfix">
      <span> {{ title }} </span>
      <el-button style="float: right; padding: 3px 0" type="text" @click="closeProof">关闭</el-button>
    </div>
    <el-row>
      <el-col>
        <div class="grid-content bg-purple">
          <div class="block" style="margin-top: 25px;">
            <video ref="flvVideo" id="flv-1" height="500" muted controls autoplay playsinline></video>
          </div>
        </div>
      </el-col>
    </el-row>
  </el-card>
</template>

<script>
import flvjs from 'flv.js';
import { extractPlayableUrl, isBrowserPlayableUrl, isFlvUrl } from '@/utils/mediaPlayback';
import webcamPusher from '@/utils/webcamPusher';

export default {
  name: 'player',
  props: {
    rtspUrl: {
      required: true,
      type: String
    },
    viewProof: {
      required: true,
      type: Boolean
    },
    title: {
      required: true,
      type: String
    },
    inline: {
      type: Boolean,
      default: false
    },
  },

  data() {
    return {
      flvPlayer: null,
      timeUpdateHandler: null,
      reconnectTimer: null,
      unsubscribeWebcam: null,
      lastCurrentTime: 0,
      stallCheckTimer: null,
      stallCount: 0
    };
  },

  computed: {
    cardStyle() {
      return this.inline ? {} : { zIndex: 1000 };
    }
  },

  created() {
    // 监听全局摄像头开关状态变动，若当前正打开预览，1.2s后自动平滑刷新流，无需用户手动关闭重开弹窗
    this.unsubscribeWebcam = webcamPusher.subscribe(() => {
      if (this.viewProof && this.rtspUrl) {
        setTimeout(() => {
          if (this.viewProof && this.rtspUrl) {
            this.initFLVPlayer();
          }
        }, 1200);
      }
    });
  },

  mounted() {
    this.$nextTick(() => {
      if (this.viewProof && this.rtspUrl) {
        this.initFLVPlayer();
      }
    });
  },

  beforeDestroy() {
    this.closeFLVPlayer(true);
    if (this.unsubscribeWebcam) {
      this.unsubscribeWebcam();
    }
  },

  methods: {
    playHttpMedia(url) {
      const videoElement = this.$refs.flvVideo;
      if (!videoElement || !url) return;
      this.closeFLVPlayer(true);
      videoElement.src = url;
      videoElement.muted = false;
      videoElement.play().catch(() => {});
    },

    playFlvMedia(url) {
      const videoElement = this.$refs.flvVideo;
      if (!videoElement || !url) return;
      this.closeFLVPlayer(true);

      if (flvjs.isSupported()) {
        this.flvPlayer = flvjs.createPlayer({
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
        });
        this.flvPlayer.attachMediaElement(videoElement);
        this.flvPlayer.load();
        const playPromise = this.flvPlayer.play();
        if (playPromise && playPromise.catch) {
          playPromise.catch(() => {});
        }

        // 追帧机制与卡流自愈
        this.timeUpdateHandler = () => {
          if (this.flvPlayer && videoElement.buffered && videoElement.buffered.length) {
            const end = videoElement.buffered.end(videoElement.buffered.length - 1);
            const diff = end - videoElement.currentTime;
            if (diff > 1.2) {
              videoElement.currentTime = end - 0.1;
            }
          }
        };
        videoElement.addEventListener('timeupdate', this.timeUpdateHandler);

        // 监测码流突变导致的画面冻结，若持续停滞超 2.5 秒则自愈重连
        this.startStallDetection(videoElement);

        this.flvPlayer.on(flvjs.Events.ERROR, (errType, errDetail) => {
          console.warn('[RTSPPlayer] FLV error:', errType, errDetail);
          if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
          this.reconnectTimer = setTimeout(() => {
            if (this.viewProof && this.rtspUrl) {
              this.initFLVPlayer();
            }
          }, 1200);
        });
      }
    },

    startStallDetection(videoElement) {
      if (this.stallCheckTimer) clearInterval(this.stallCheckTimer);
      this.stallCount = 0;
      this.lastCurrentTime = videoElement ? videoElement.currentTime : 0;
      this.stallCheckTimer = setInterval(() => {
        if (!this.viewProof || !this.flvPlayer || !videoElement) {
          if (this.stallCheckTimer) clearInterval(this.stallCheckTimer);
          return;
        }
        if (Math.abs(videoElement.currentTime - this.lastCurrentTime) < 0.05 && !videoElement.paused) {
          this.stallCount += 1;
          if (this.stallCount >= 3) {
            this.stallCount = 0;
            this.initFLVPlayer();
          }
        } else {
          this.stallCount = 0;
          this.lastCurrentTime = videoElement.currentTime;
        }
      }, 1000);
    },

    initFLVPlayer() {
      if (!this.viewProof || !this.rtspUrl) {
        return;
      }
      const videoElement = this.$refs.flvVideo;
      const url = extractPlayableUrl(this.rtspUrl);
      if (!videoElement || !isBrowserPlayableUrl(url)) {
        this.closeFLVPlayer(true);
        return;
      }

      if (isFlvUrl(url)) {
        this.playFlvMedia(url);
      } else {
        this.playHttpMedia(url);
      }
    },

    closeFLVPlayer(realClose) {
      if (this.reconnectTimer) {
        clearTimeout(this.reconnectTimer);
        this.reconnectTimer = null;
      }
      if (this.stallCheckTimer) {
        clearInterval(this.stallCheckTimer);
        this.stallCheckTimer = null;
      }
      const videoElement = this.$refs.flvVideo;
      if (videoElement && this.timeUpdateHandler) {
        videoElement.removeEventListener('timeupdate', this.timeUpdateHandler);
        this.timeUpdateHandler = null;
      }

      if (this.flvPlayer != null) {
        try {
          this.flvPlayer.pause();
          this.flvPlayer.unload();
          this.flvPlayer.detachMediaElement();
          this.flvPlayer.destroy();
        } catch (e) {
          // ignore
        }
        this.flvPlayer = null;
      }

      if (videoElement) {
        videoElement.pause();
        videoElement.removeAttribute('src');
        videoElement.load();
      }
    },

    closeProof() {
      this.closeFLVPlayer(true);
      this.$emit('closeProof');
    }
  },

  watch: {
    rtspUrl(newVal) {
      if (this.viewProof && newVal) {
        this.$nextTick(() => {
          this.initFLVPlayer();
        });
      }
    },

    viewProof(newVal) {
      if (newVal === true) {
        this.$nextTick(() => {
          this.initFLVPlayer();
        });
      } else {
        this.closeFLVPlayer(true);
      }
    }
  }
};
</script>

<style lang="scss" scoped>
.text {
  font-size: 14px;
}

.item {
  margin-bottom: 18px;
}

.clearfix:before,
.clearfix:after {
  display: table;
  content: "";
}

.clearfix:after {
  clear: both
}

.box-card {
  position: fixed;
  top: 100px;
  left: 50%;
  transform: translateX(-50%);
  width: 1030px;
  height: 620px;
  z-index: 1000;
}

.box-card--inline {
  position: static;
  top: auto;
  left: auto;
  transform: none;
  width: 100%;
  height: auto;
}

.box-card--inline ::v-deep video {
  width: 100%;
  height: 320px;
}
</style>
