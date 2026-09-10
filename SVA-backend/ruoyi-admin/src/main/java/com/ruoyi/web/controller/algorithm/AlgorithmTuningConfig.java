package com.ruoyi.web.controller.algorithm;

import lombok.Data;

/**
 * YOLO-Pose 算法参数动态热加载配置
 *
 * <p>字段命名与 C++ SVA-server 的 /api/control/update-algorithm-config 接口严格对齐，
 * 数值类型使用包装类，便于区分“未设置(不下发)”与“设置为 0”。</p>
 */
@Data
public class AlgorithmTuningConfig
{
    /** 预设档位：HIGH 高灵敏 / MEDIUM 标准 / LOW 宽松 / CUSTOM 专家自定义 */
    private String preset;

    /** 目标布控编号，空或 * 表示对所有运行中布控批量生效 */
    private String controlCode;

    /** 目标检出置信度阈值 (0.1~0.9) */
    private Double detectionConfidence;

    /** NMS 交并比阈值 (0.1~0.9) */
    private Double nmsThreshold;

    /** 姿态关键点置信度阈值 */
    private Double keypointConfidence;

    /** 睡岗判定确认窗口（秒），内部换算为毫秒 */
    private Double confirmWindowSec;

    /** 判定为睡岗所需的正样本帧比例 (0.5~1.0) */
    private Double sleepPositiveRatio;

    /** 头肩垂直高度比上限 */
    private Double headHeightRatioMax;

    /** 头臂距离比上限 */
    private Double headArmDistanceRatioMax;

    /** 头部微动位移比上限 */
    private Double headMotionRatioMax;

    /** 苏醒防抖恢复时间（毫秒） */
    private Long recoveryMs;

    /** 躯干倾角下限（度） */
    private Double torsoAngleDegMin;

    /** 肩部倾斜角下限（度） */
    private Double shoulderTiltDegMin;

    /** 运动统计窗口（毫秒） */
    private Long motionWindowMs;
}
