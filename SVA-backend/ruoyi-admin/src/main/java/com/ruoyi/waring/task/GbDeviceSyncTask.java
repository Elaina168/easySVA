package com.ruoyi.waring.task;

import com.ruoyi.waring.service.HDeviceService;
import jakarta.annotation.Resource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * GB28181 国标设备定时同步任务。
 * <p>
 * 定时从 ZLMediaKit 拉取国标设备列表并同步到本地设备表（含在线/离线状态）。
 * 使用 AtomicBoolean 防止上一轮同步未结束时重复执行。
 */
@Component("gbDeviceSyncTask")
public class GbDeviceSyncTask {

    private static final Logger log = LoggerFactory.getLogger(GbDeviceSyncTask.class);

    private final AtomicBoolean syncing = new AtomicBoolean(false);

    @Resource
    private HDeviceService hDeviceService;

    /**
     * 每 60 秒同步一次国标设备（含状态）。
     */
    @Scheduled(initialDelay = 60000L, fixedDelay = 60000L)
    public void scheduledSyncGbDevices() {
        if (!syncing.compareAndSet(false, true)) {
            return;
        }
        try {
            int count = hDeviceService.syncGbDevices();
            log.info("[GB28181] 定时同步国标设备完成，共处理 {} 台", count);
        } catch (Exception e) {
            log.warn("[GB28181] 定时同步国标设备失败: {}", e.getMessage());
        } finally {
            syncing.set(false);
        }
    }
}
