<template>
  <div class="avb">
    <el-image class="img" :src="img1" fit="contain"></el-image>
    <el-text>{{ algorithm }}</el-text>
  </div>
</template>

<script>
export default {
  data() {
    return {
      img1: '/activity.png'
    }
  },
  computed: {
    algorithm() {
      return this.$store.state.algorithm
    }
  }
}
</script>

<style lang='scss' scoped>
.avb {
  width: 95%;
  margin-left: 25px;
  // margin-top: 15px;
}

.img {
  margin-top: 10px;
  width: 100%;
  height: 230px;
}

</style>
