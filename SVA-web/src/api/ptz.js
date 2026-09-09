import request from '@/utils/request'

// 云台控制接口 (PTZ)
export function ptzControl(apeId, data) {
  return request({
    url: '/waring/device/ptz/' + apeId,
    method: 'post',
    data: data
  })
}
