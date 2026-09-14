package com.ruoyi.web.controller.algorithm;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.ruoyi.common.core.controller.BaseController;
import com.ruoyi.common.core.domain.AjaxResult;
import com.ruoyi.common.utils.StringUtils;
import com.ruoyi.waring.domain.SvaServer;
import com.ruoyi.waring.mapper.SvaServerMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.HttpEntity;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.client.RestTemplate;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * YOLO-Pose 算法参数动态热加载
 *
 * <p>前端在实时监控面板选择灵敏度档位 / 专家滑块后，由本控制器把参数实时下发给
 * C++ SVA-server 的 /api/control/update-algorithm-config，分析流不中断、无需重启。</p>
 */
@RestController
@RequestMapping("/waring/algorithm/config")
public class AlgorithmTuningController extends BaseController
{
    private static final Logger log = LoggerFactory.getLogger(AlgorithmTuningController.class);
    private static final ObjectMapper OBJECT_MAPPER = new ObjectMapper();
    private static final Long DEFAULT_SVA_SERVER_ID = 1L;

    @Autowired
    private RestTemplate restTemplate;

    @Autowired
    private SvaServerMapper svaServerMapper;

    /** 仅保存本次后端运行期间的最近一次更新回执，不冒充引擎实时配置。 */
    private volatile Map<String, Object> lastApplied;


    /**
     * 查询编辑默认值、历史回执、预设档位及允许的参数范围
     */
    @GetMapping
    public AjaxResult current()
    {
        Map<String, Object> data = new LinkedHashMap<>();
        data.put("current", null);
        data.put("configurationState", "UNVERIFIED");
        data.put("defaultConfig", buildPreset("MEDIUM"));
        data.put("lastApplied", lastApplied);
        data.put("persistence", "RUNTIME_ONLY");
        data.put("presets", buildPresetMeta());
        data.put("ranges", buildRanges());
        return AjaxResult.success(data);
    }

    /**
     * 提交热加载：校验参数补丁 -> 下发 C++ 引擎 -> 保存实际更新回执
     */
    @PostMapping("/update")
    public synchronized AjaxResult update(@RequestBody AlgorithmTuningConfig request)
    {
        if (request == null)
        {
            return AjaxResult.error("参数不能为空");
        }

        // 1. 选择预设档位时，用档位模板覆盖具体数值；专家自定义则保留前端传入值
        String preset = StringUtils.isBlank(request.getPreset()) ? "CUSTOM" : request.getPreset().trim().toUpperCase();
        AlgorithmTuningConfig merged;
        if ("HIGH".equals(preset) || "MEDIUM".equals(preset) || "LOW".equals(preset))
        {
            merged = buildPreset(preset);
            // 预设仍允许指定布控范围
            merged.setControlCode(StringUtils.isBlank(request.getControlCode()) ? "*" : request.getControlCode());
        }
        else
        {
            merged = OBJECT_MAPPER.convertValue(request, AlgorithmTuningConfig.class);
            preset = "CUSTOM";
            if (StringUtils.isBlank(merged.getControlCode()))
            {
                merged.setControlCode("*");
            }
        }
        merged.setPreset(preset);

        // 模型阈值是全局参数。单任务预设只下发行为规则；显式提交全局值则拒绝。
        if (!"*".equals(merged.getControlCode()))
        {
            if ("CUSTOM".equals(preset) && (merged.getDetectionConfidence() != null || merged.getNmsThreshold() != null))
                return AjaxResult.error("目标检出置信度和 NMS 为共享模型参数，只能在全部布控范围修改");
            merged.setDetectionConfidence(null);
            merged.setNmsThreshold(null);
        }
        String validationError = validate(merged);
        if (validationError != null) return AjaxResult.error(validationError);

        // 仅下发用户提供的补丁字段，不以默认值覆盖未提交的参数。
        Map<String, Object> payload = new LinkedHashMap<>();
        payload.put("controlCode", StringUtils.isBlank(merged.getControlCode()) ? "*" : merged.getControlCode());
        putPositive(payload, "detectionConfidence", merged.getDetectionConfidence());
        putPositive(payload, "nmsThreshold", merged.getNmsThreshold());
        putPositive(payload, "keypointConfidence", merged.getKeypointConfidence());
        putPositive(payload, "confirmWindowSec", merged.getConfirmWindowSec());
        putPositive(payload, "sleepPositiveRatio", merged.getSleepPositiveRatio());
        putPositive(payload, "headHeightRatioMax", merged.getHeadHeightRatioMax());
        putPositive(payload, "headArmDistanceRatioMax", merged.getHeadArmDistanceRatioMax());
        putPositive(payload, "headMotionRatioMax", merged.getHeadMotionRatioMax());
        putPositiveLong(payload, "recoveryMs", merged.getRecoveryMs());
        putPositive(payload, "torsoAngleDegMin", merged.getTorsoAngleDegMin());
        putPositive(payload, "shoulderTiltDegMin", merged.getShoulderTiltDegMin());
        putPositiveLong(payload, "motionWindowMs", merged.getMotionWindowMs());

        if (payload.size() == 1) return AjaxResult.error("请至少提交一个调节参数");

        // 2. 定位 C++ 分析引擎地址
        SvaServer sva = svaServerMapper.selectEnabledById(DEFAULT_SVA_SERVER_ID);
        if (sva == null || StringUtils.isBlank(sva.getHost()) || sva.getAnalyzer_port() == null)
        {
            return AjaxResult.error("未找到可用的算法服务器(sva_server)配置");
        }
        String engineUrl = "http://" + sva.getHost().trim() + ":" + sva.getAnalyzer_port()
            + "/api/control/update-algorithm-config";

        // 下发并核验实际更新任务。
        try
        {
            HttpHeaders headers = new HttpHeaders();
            headers.setContentType(MediaType.APPLICATION_JSON);
            HttpEntity<Map<String, Object>> entity = new HttpEntity<>(payload, headers);
            ResponseEntity<String> response = restTemplate.postForEntity(engineUrl, entity, String.class);
            String body = response.getBody();
            log.info("算法参数热加载下发, url={}, payload={}, resp={}", engineUrl, payload, body);

            if (!response.getStatusCode().is2xxSuccessful() || StringUtils.isBlank(body))
            {
                return AjaxResult.error("算法引擎返回异常，HTTP=" + response.getStatusCode().value());
            }
            JsonNode root = OBJECT_MAPPER.readTree(body);
            int code = root.path("code").asInt(0);
            String msg = root.path("msg").asText("");
            if (code != 1000)
            {
                return AjaxResult.error("热加载未生效：" + (StringUtils.isBlank(msg) ? "引擎返回 code=" + code : msg));
            }

            JsonNode controls = root.path("updatedControls");
            JsonNode global = root.path("globalThresholdsUpdated");
            boolean expectedGlobal = payload.containsKey("detectionConfidence") || payload.containsKey("nmsThreshold");
            if (!controls.isArray() || controls.isEmpty() || !global.isBoolean()
                || global.asBoolean() != expectedGlobal)
                return AjaxResult.error("引擎未提供完整生效回执，请核对引擎版本和运行布控；参数状态未确认");
            List<String> updatedControls = new ArrayList<>();
            for (JsonNode item : controls)
            {
                if (!item.isTextual() || StringUtils.isBlank(item.asText())
                    || (!"*".equals(merged.getControlCode()) && !merged.getControlCode().equals(item.asText())))
                    return AjaxResult.error("引擎生效任务与请求范围不一致，参数状态未确认");
                if (!updatedControls.contains(item.asText())) updatedControls.add(item.asText());
            }
            Map<String, Object> receipt = new LinkedHashMap<>();
            receipt.put("preset", preset);
            receipt.put("parameters", new LinkedHashMap<>(payload));
            receipt.put("updatedControls", updatedControls);
            receipt.put("globalThresholdsUpdated", global.asBoolean());
            receipt.put("appliedAt", java.time.Instant.now().toString());
            lastApplied = receipt;
            AjaxResult ok = AjaxResult.success("本次已更新 " + updatedControls.size() + " 个运行中的睡岗布控"
                + (global.asBoolean() ? "；共享 Pose 模型阈值已更新" : "；共享模型阈值未修改"));
            ok.put("updatedControls", updatedControls);
            ok.put("globalThresholdsUpdated", global.asBoolean());
            ok.put("lastApplied", receipt);
            return ok;
        }
        catch (Exception ex)
        {
            log.error("算法参数热加载下发失败, url={}", engineUrl, ex);
            return AjaxResult.error("无法连接算法分析引擎，请确认 Analyzer 已启动：" + ex.getMessage());
        }
    }

    /** 在访问引擎前校验字段，避免非法值被静默丢弃后显示成功。 */
    private String validate(AlgorithmTuningConfig config)
    {
        JsonNode values = OBJECT_MAPPER.valueToTree(config);
        Map<String, Object> ranges = buildRanges();
        for (Map.Entry<String, Object> entry : ranges.entrySet())
        {
            JsonNode value = values.path(entry.getKey());
            if (value.isMissingNode() || value.isNull()) continue;
            Map<?, ?> range = (Map<?, ?>) entry.getValue();
            double number = value.asDouble();
            double min = ((Number) range.get("min")).doubleValue();
            double max = ((Number) range.get("max")).doubleValue();
            if (!value.isNumber() || !Double.isFinite(number) || number < min || number > max)
                return entry.getKey() + " 超出允许范围 [" + min + ", " + max + "]";
        }
        return null;
    }

    private void putPositive(Map<String, Object> payload, String key, Double value)
    {
        if (value != null && value > 0)
        {
            payload.put(key, value);
        }
    }

    private void putPositiveLong(Map<String, Object> payload, String key, Long value)
    {
        if (value != null && value > 0)
        {
            payload.put(key, value);
        }
    }

    /** 三档预设模板（专家档 CUSTOM 不预填，由滑块决定） */
    private AlgorithmTuningConfig buildPreset(String level)
    {
        AlgorithmTuningConfig c = new AlgorithmTuningConfig();
        c.setControlCode("*");
        // 公共几何/防抖参数保持与 C++ 默认一致
        c.setNmsThreshold(0.45);
        c.setKeypointConfidence(0.35);
        c.setHeadArmDistanceRatioMax(0.75);
        c.setHeadMotionRatioMax(0.15);
        c.setRecoveryMs(2000L);
        c.setTorsoAngleDegMin(25.0);
        c.setShoulderTiltDegMin(15.0);
        c.setMotionWindowMs(2000L);

        switch (level)
        {
            case "HIGH":
                c.setPreset("HIGH");
                c.setConfirmWindowSec(3.0);
                c.setHeadHeightRatioMax(0.52);
                c.setDetectionConfidence(0.25);
                c.setSleepPositiveRatio(0.70);
                break;
            case "LOW":
                c.setPreset("LOW");
                c.setConfirmWindowSec(30.0);
                c.setHeadHeightRatioMax(0.42);
                c.setDetectionConfidence(0.40);
                c.setSleepPositiveRatio(0.85);
                break;
            case "MEDIUM":
            default:
                c.setPreset("MEDIUM");
                c.setConfirmWindowSec(15.0);
                c.setHeadHeightRatioMax(0.48);
                c.setDetectionConfidence(0.35);
                c.setSleepPositiveRatio(0.80);
                break;
        }
        return c;
    }

    /** 预设档位元信息，供前端渲染单选卡片 */
    private List<Map<String, Object>> buildPresetMeta()
    {
        List<Map<String, Object>> list = new ArrayList<>();
        list.add(preset("HIGH", "高灵敏模式", "确认窗口3秒，实际告警取决于姿态及有效帧", "danger"));
        list.add(preset("MEDIUM", "标准模式", "确认窗口15秒，需结合机位验证误报与漏报", "primary"));
        list.add(preset("LOW", "长窗口模式", "确认窗口30秒，不保证消除误报", "success"));
        list.add(preset("CUSTOM", "专家自定义", "展开滑块逐项微调，适配特定机位角度", "warning"));
        return list;
    }

    private Map<String, Object> preset(String key, String label, String desc, String tagType)
    {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("key", key);
        m.put("label", label);
        m.put("desc", desc);
        m.put("tagType", tagType);
        return m;
    }

    /** 专家滑块取值范围与中文说明 */
    private Map<String, Object> buildRanges()
    {
        Map<String, Object> r = new LinkedHashMap<>();
        r.put("confirmWindowSec", range(1, 60, 1, "睡岗确认窗口(秒)", "持续低头达到该时长才告警，越小越灵敏"));
        r.put("detectionConfidence", range(0.1, 0.9, 0.01, "目标检出置信度", "越低越容易检出，过高可能漏检"));
        r.put("nmsThreshold", range(0.1, 0.9, 0.01, "NMS 交并比阈值", "重叠框去重强度"));
        r.put("keypointConfidence", range(0.1, 0.9, 0.01, "关键点置信度", "姿态关键点有效门槛"));
        r.put("sleepPositiveRatio", range(0.5, 1.0, 0.01, "睡岗正样本比例", "确认窗口内低头帧占比达到该值才判睡"));
        r.put("headHeightRatioMax", range(0.3, 0.7, 0.01, "头肩高度比上限", "头部相对肩部越低垂，比值越小"));
        r.put("headArmDistanceRatioMax", range(0.3, 1.2, 0.01, "头臂距离比上限", "趴桌时头部靠近手臂的程度阈值"));
        r.put("headMotionRatioMax", range(0.05, 0.6, 0.01, "头部微动比上限", "头部静止程度阈值，越小要求越静止"));
        r.put("recoveryMs", range(500, 8000, 100, "苏醒恢复时间(毫秒)", "重新抬头后多久解除告警状态"));
        r.put("torsoAngleDegMin", range(5, 60, 1, "躯干倾角下限(度)", "躯干前倾超过该角度计入睡岗姿态"));
        r.put("shoulderTiltDegMin", range(5, 45, 1, "肩部倾斜下限(度)", "肩部倾斜超过该角度计入睡岗姿态"));
        r.put("motionWindowMs", range(500, 6000, 100, "运动统计窗口(毫秒)", "统计头部微动的时间窗"));
        return r;
    }

    private Map<String, Object> range(double min, double max, double step, String label, String tip)
    {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("min", min);
        m.put("max", max);
        m.put("step", step);
        m.put("label", label);
        m.put("tip", tip);
        return m;
    }
}
