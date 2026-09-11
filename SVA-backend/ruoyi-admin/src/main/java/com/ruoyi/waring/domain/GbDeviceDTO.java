package com.ruoyi.waring.domain;

import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.Getter;
import lombok.NoArgsConstructor;
import com.fasterxml.jackson.annotation.JsonProperty;
import lombok.Setter;

/**
 * GB28181 国标设备数据传输对象。
 * 字段约定以任务三（GB28181/ZLMediaKit 模块）实际调通的设备列表接口为准。
 */
@Data
@Getter
@Setter
@AllArgsConstructor
@NoArgsConstructor
public class GbDeviceDTO {

    /** GB28181 设备ID（国标编码） */
    private String deviceId;

    /** GB28181 平台ID */
    private String platformId;

    /** 设备名称 */
    private String name;

    /** 设备在线状态：online / offline */
    private String status;

    /** 流ID */
    private String streamId;

    /** 播放地址 */
    private String playUrl;

    /** 最后心跳时间（Unix时间戳） */
    @JsonProperty("last_heartbeat_at")
    private Long lastHeartbeatAt;

    /** 最后注册时间（Unix时间戳） */
    @JsonProperty("last_register_at")
    private Long lastRegisterAt;

    /** 在线状态（布尔） */
    @JsonProperty("online")
    private Boolean online;
}
