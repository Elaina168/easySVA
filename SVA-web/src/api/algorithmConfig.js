import request from '@/utils/request'

// 查询当前算法热加载配置、预设档位与专家滑块范围
export function getAlgorithmTuningConfig() {
  return request({
    url: '/waring/algorithm/config',
    method: 'get'
  })
}

// 提交算法参数并实时热加载到 C++ 分析引擎（分析流不中断、无需重启）
export function updateAlgorithmTuning(data) {
  return request({
    url: '/waring/algorithm/config/update',
    method: 'post',
    data
  })
}
