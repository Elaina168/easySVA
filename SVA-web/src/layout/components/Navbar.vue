<template>
  <div class="navbar">
    <hamburger id="hamburger-container" :is-active="sidebar.opened" class="hamburger-container"
               @toggleClick="toggleSideBar"/>

    <breadcrumb id="breadcrumb-container" class="breadcrumb-container" v-if="!topNav"/>
    <top-nav id="topmenu-container" class="topmenu-container" v-if="topNav"/>

    <div class="right-menu">
      <!-- 全局电脑摄像头推流常驻控制 -->
      <el-tooltip :content="isWebcamActive ? '电脑摄像头推流中（点击可停止）' : '开启工位电脑摄像头推流（全系统各模块共享）'" placement="bottom">
        <el-button
          size="mini"
          round
          :type="isWebcamActive ? 'danger' : 'success'"
          :icon="isWebcamActive ? 'el-icon-video-pause' : 'el-icon-video-camera'"
          style="margin-right: 14px;"
          @click="toggleGlobalWebcam"
        >
          {{ isWebcamActive ? '停止电脑摄像头' : '开启电脑摄像头' }}
        </el-button>
      </el-tooltip>

      <div class="right-menu-item hover-effect dp-btn" @click="jump2DP">
        <i class="el-icon-data-line"></i>
        <span>大屏</span>
      </div>

      <el-dropdown class="avatar-container right-menu-item hover-effect" trigger="click">
        <div class="avatar-wrapper">
          <img :src="avatar" class="user-avatar">
          <i class="el-icon-caret-bottom"/>
        </div>
        <el-dropdown-menu slot="dropdown">
          <router-link to="/user/profile">
            <el-dropdown-item>个人中心</el-dropdown-item>
          </router-link>
          <el-dropdown-item divided @click.native="logout">
            <span>退出登录</span>
          </el-dropdown-item>
        </el-dropdown-menu>
      </el-dropdown>
    </div>
  </div>
</template>

<script>
import {mapGetters} from 'vuex'
import Breadcrumb from '@/components/Breadcrumb'
import TopNav from '@/components/TopNav'
import Hamburger from '@/components/Hamburger'
import Screenfull from '@/components/Screenfull'
import SizeSelect from '@/components/SizeSelect'
import Search from '@/components/HeaderSearch'
import RuoYiGit from '@/components/RuoYi/Git'
import RuoYiDoc from '@/components/RuoYi/Doc'
import webcamPusher from '@/utils/webcamPusher'

export default {
  components: {
    Breadcrumb,
    TopNav,
    Hamburger,
    Screenfull,
    SizeSelect,
    Search,
    RuoYiGit,
    RuoYiDoc
  },
  data() {
    return {
      isWebcamActive: false,
      unsubscribeWebcam: null
    }
  },
  created() {
    this.unsubscribeWebcam = webcamPusher.subscribe(active => {
      this.isWebcamActive = active
    })
  },
  beforeDestroy() {
    if (this.unsubscribeWebcam) {
      this.unsubscribeWebcam()
    }
  },

  computed: {
    ...mapGetters([
      'sidebar',
      'avatar',
      'device'
    ]),
    setting: {
      get() {
        return this.$store.state.settings.showSettings
      },
      set(val) {
        this.$store.dispatch('settings/changeSetting', {
          key: 'showSettings',
          value: val
        })
      }
    },
    topNav: {
      get() {
        return this.$store.state.settings.topNav
      }
    }
  },
  methods: {
    async toggleGlobalWebcam() {
      try {
        const active = await webcamPusher.toggle()
        if (active) {
          this.$message.success('已开启工位电脑摄像头推流！可在【实时监控】、【设备管理视频预览】全系统使用。')
        } else {
          this.$message.info('已停止电脑摄像头推流。')
        }
      } catch (err) {
        this.$message.error('开启电脑摄像头失败: ' + (err.message || '请检查浏览器权限'))
      }
    },
    jump2End() {
      const currentProtocol = window.location.protocol;
      const currentHost = window.location.hostname;
      const newPort = '9111';
      const newUrl = `${currentProtocol}//${currentHost}:${newPort}/login`;
      window.open(newUrl, '后台');
    },

    jump2DP() {
      this.$router.push({path: "/dping"}).catch(() => {
      });
    },

    toggleSideBar() {
      this.$store.dispatch('app/toggleSideBar')
    },
    async logout() {
      this.$modal.confirm('确定注销并退出系统吗？').then(() => {
        this.$store.dispatch('LogOut').then(() => {
          location.href = '/index';
        });
      }).catch(() => {
      });
    }
  }
}
</script>

<style lang="scss" scoped>
.app-breadcrumb.el-breadcrumb {
  cursor: text;
  font-size: 18px;
}

.navbar {
  background-color: white;
  height: 50px;
  overflow: hidden;
  position: relative;
  box-shadow: 0 1px 4px rgba(0, 21, 41, .08);

  .hamburger-container {
    line-height: 46px;
    height: 100%;
    float: left;
    cursor: pointer;
    transition: background .3s;
    -webkit-tap-highlight-color: transparent;

    &:hover {
      background: rgba(0, 0, 0, .025)
    }
  }

  .breadcrumb-container {
    float: left;
  }

  .topmenu-container {
    position: absolute;
    left: 50px;
  }

  .errLog-container {
    display: inline-block;
    vertical-align: top;
  }

  .right-menu {
    float: right;
    height: 100%;
    line-height: 50px;
    display: flex;
    align-items: center;
    padding-right: 20px;

    &:focus {
      outline: none;
    }

    .dp-btn {
      display: inline-flex;
      align-items: center;
      height: 30px;
      line-height: 30px;
      padding: 0 10px;
      margin-right: 14px;
      font-size: 14px;
      color: #5a5e66;
      border-radius: 4px;
      cursor: pointer;
      user-select: none;

      i {
        font-size: 16px;
        margin-right: 4px;
      }

      &:hover {
        color: #1890ff;
        background: rgba(0, 0, 0, 0.035);
      }
    }

    .avatar-container {
      display: inline-flex;
      align-items: center;

      .avatar-wrapper {
        display: inline-flex;
        align-items: center;
        position: relative;
        cursor: pointer;

        .user-avatar {
          width: 34px;
          height: 34px;
          border-radius: 8px;
        }

        .el-icon-caret-bottom {
          margin-left: 6px;
          font-size: 12px;
          color: #909399;
        }
      }
    }
  }
}
</style>
