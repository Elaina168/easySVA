package com.ruoyi.waring.service.impl;


import com.github.pagehelper.PageHelper;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.ruoyi.common.exception.ServiceException;
import com.ruoyi.common.core.domain.entity.SysDept;
import com.ruoyi.common.core.domain.entity.SysUser;
import com.ruoyi.common.core.page.PageDomain;
import com.ruoyi.common.core.page.TableSupport;
import com.ruoyi.common.utils.StringUtils;
import com.ruoyi.system.mapper.SysDeptMapper;
import com.ruoyi.system.mapper.SysUserMapper;
import com.ruoyi.waring.domain.GbDeviceDTO;
import com.ruoyi.waring.domain.HDevice;
import com.ruoyi.waring.domain.ZlmServer;
import com.ruoyi.waring.mapper.HDeviceMapper;
import com.ruoyi.waring.mapper.ZlmServerMapper;
import com.ruoyi.waring.service.HDeviceService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.ResponseEntity;
import org.springframework.web.client.RestTemplate;
import org.springframework.dao.DuplicateKeyException;
import org.springframework.web.util.UriComponentsBuilder;
import org.springframework.stereotype.Component;
import org.springframework.stereotype.Service;

import jakarta.annotation.PostConstruct;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ThreadLocalRandom;
import java.util.regex.Pattern;


@Service
@Component
public class HDeviceServiceImpl implements HDeviceService {

    private static final Logger log = LoggerFactory.getLogger(HDeviceServiceImpl.class);

    private static final String STREAM_SOURCE_TYPE_DIRECT = "DIRECT";
    private static final String STREAM_SOURCE_TYPE_PLATFORM = "PLATFORM";
    private static final int MAX_APE_ID_GENERATE_RETRY = 20;
    private static final Pattern STREAM_NAME_PATTERN = Pattern.compile("[^A-Za-z0-9_-]");
    private static final ObjectMapper OBJECT_MAPPER = new ObjectMapper();
    private static final String MONITOR_STATUS_RUNNING = "RUNNING";
    private static final String MONITOR_STATUS_STOPPED = "STOPPED";
    private static final String MONITOR_STATUS_STARTING = "STARTING";
    private static final String MONITOR_STATUS_STOPPING = "STOPPING";
    private static final String MONITOR_STATUS_ERROR = "ERROR";
    private static final long DEFAULT_SERVER_ID = 1L;
    private static final String DEFAULT_ZLM_APP = "live";

    @Autowired
    HDeviceMapper hDeviceMapper;

    @Autowired
    SysUserMapper userMapper;

    @Autowired
    SysDeptMapper sysDeptMapper;

    @Autowired
    ZlmServerMapper zlmServerMapper;

    @Autowired(required = false)
    private RestTemplate restTemplate;

    @PostConstruct
    private void initRestTemplate() {
        if (restTemplate == null) {
            restTemplate = new RestTemplate();
        }
    }

    @Override
    public void insertDevice(HDevice device) {
        hDeviceMapper.insertDevice(device);
    }

    @Override
    public void deleteDevice() {
        hDeviceMapper.deleteDevice();
    }

    @Override
    public HDevice selectDeviceByApeId(String apeId) {
        return hDeviceMapper.selectDeviceByApeId(apeId);
    }

    @Override
    public int insertDeviceCrud(HDevice device) {
        normalizeStreamSourceType(device, null);
        validateStreamSourceRule(device, null);
        if (StringUtils.isBlank(device.getOrg_name())) {
            throw new ServiceException("组织名称不能为空");
        }
        device.setOrg_index(normalizeOrgIndex(device.getOrg_index()));

        if (StringUtils.isBlank(device.getApe_id())) {
            device.setApe_id(generateUniqueApeId());
        } else if (hDeviceMapper.selectDeviceByApeId(device.getApe_id()) != null) {
            throw new ServiceException("设备编码已存在: " + device.getApe_id());
        }

        return hDeviceMapper.insertDeviceCrud(device);
    }

    @Override
    public int updateDevice(HDevice device) {
        if (StringUtils.isBlank(device.getApe_id())) {
            throw new ServiceException("设备编码不能为空");
        }

        HDevice existedDevice = hDeviceMapper.selectDeviceByApeId(device.getApe_id());
        if (existedDevice == null) {
            throw new ServiceException("设备不存在: " + device.getApe_id());
        }

        normalizeStreamSourceType(device, existedDevice);
        validateStreamSourceRule(device, existedDevice);
        device.setOrg_index(normalizeOrgIndex(device.getOrg_index()));

        return hDeviceMapper.updateDevice(device);
    }

    private void normalizeStreamSourceType(HDevice device, HDevice existedDevice) {
        String streamSourceType = device.getStream_source_type();
        if (StringUtils.isBlank(streamSourceType) && existedDevice != null) {
            streamSourceType = existedDevice.getStream_source_type();
        }
        if (StringUtils.isBlank(streamSourceType)) {
            streamSourceType = STREAM_SOURCE_TYPE_DIRECT;
        }

        streamSourceType = StringUtils.upperCase(streamSourceType.trim());
        if (!STREAM_SOURCE_TYPE_DIRECT.equals(streamSourceType) && !STREAM_SOURCE_TYPE_PLATFORM.equals(streamSourceType)) {
            throw new ServiceException("stream_source_type 仅支持 PLATFORM 或 DIRECT");
        }
        device.setStream_source_type(streamSourceType);
    }

    private void validateStreamSourceRule(HDevice device, HDevice existedDevice) {
        if (!STREAM_SOURCE_TYPE_DIRECT.equals(device.getStream_source_type())) {
            return;
        }

        String finalName = pickFinalValue(device.getName(), existedDevice == null ? null : existedDevice.getName());
        if (StringUtils.isBlank(finalName)) {
            throw new ServiceException("DIRECT 设备类型下，name 不能为空");
        }

        String finalDirectSourceUrl = pickFinalValue(device.getDirect_source_url(), existedDevice == null ? null : existedDevice.getDirect_source_url());
        if (StringUtils.isBlank(finalDirectSourceUrl)) {
            throw new ServiceException("DIRECT 设备类型下，direct_source_url 不能为空");
        }
    }

    private String pickFinalValue(String incomingValue, String existedValue) {
        if (incomingValue != null) {
            return incomingValue;
        }
        return existedValue;
    }

    private String generateUniqueApeId() {
        for (int i = 0; i < MAX_APE_ID_GENERATE_RETRY; i++) {
            String candidate = "cam" + String.format("%06d", ThreadLocalRandom.current().nextInt(1000000));
            if (hDeviceMapper.selectDeviceByApeId(candidate) == null) {
                return candidate;
            }
        }
        throw new ServiceException("自动生成设备编码失败，请稍后重试");
    }

    @Override
    public int deleteDeviceByApeIds(String[] apeIds) {
        return hDeviceMapper.deleteDeviceByApeIds(apeIds);
    }

    @Override
    public Map<String, Object> getDirectLiveUrl(String apeId) {
        if (StringUtils.isBlank(apeId)) {
            throw new ServiceException("apeId 不能为空");
        }

        HDevice device = hDeviceMapper.selectDeviceByApeId(apeId);
        if (device == null) {
            throw new ServiceException("设备不存在: " + apeId);
        }

        if (!STREAM_SOURCE_TYPE_DIRECT.equalsIgnoreCase(device.getStream_source_type())) {
            throw new ServiceException("仅支持 DIRECT 设备类型");
        }

        if (StringUtils.isBlank(device.getDirect_source_url())) {
            throw new ServiceException("DIRECT 设备类型下，direct_source_url 不能为空");
        }

        Long zlmServerId = device.getZlm_server_id() == null ? DEFAULT_SERVER_ID : device.getZlm_server_id();
        ZlmServer zlmServer = zlmServerMapper.selectEnabledById(zlmServerId);
        if (zlmServer == null) {
            throw new ServiceException("设备未绑定可用ZLM服务器");
        }
        if (StringUtils.isBlank(zlmServer.getHost()) || zlmServer.getApi_port() == null || zlmServer.getMedia_http_port() == null) {
            throw new ServiceException("可用ZLM服务器配置缺失");
        }

        String zlmApp = StringUtils.isBlank(zlmServer.getApp()) ? DEFAULT_ZLM_APP : zlmServer.getApp().trim();

        String stream = sanitizeStreamName(apeId);
        String addProxyUrl = UriComponentsBuilder
            .fromUriString("http://" + zlmServer.getHost() + ":" + zlmServer.getApi_port() + "/index/api/addStreamProxy")
                .queryParam("vhost", "__defaultVhost__")
                .queryParam("app", zlmApp)
                .queryParam("stream", stream)
                .queryParam("url", device.getDirect_source_url())
            .queryParam("enable_mp4", 1)
            .queryParam("auto_close", 0)
                .queryParamIfPresent("secret", StringUtils.isNotBlank(zlmServer.getSecret())
                        ? java.util.Optional.of(zlmServer.getSecret())
                        : java.util.Optional.empty())
                .build(true)
                .toUriString();

            if (log.isDebugEnabled()) {
                log.debug("调用ZLM addStreamProxy, apeId={}, url={}", apeId, maskSensitiveUrl(addProxyUrl));
            }

        ResponseEntity<String> response = restTemplate.getForEntity(addProxyUrl, String.class);
        String body = response.getBody();
        if (StringUtils.isBlank(body)) {
            throw new ServiceException("调用 ZLM addStreamProxy 失败: empty response");
        }

        int code;
        String msg;
        String zlmProxyKey;
        try {
            JsonNode root = OBJECT_MAPPER.readTree(body);
            code = parseCode(root.path("code").asText());
            msg = root.path("msg").asText("");
            zlmProxyKey = root.path("data").path("key").asText("");
        } catch (Exception e) {
            throw new ServiceException("调用 ZLM addStreamProxy 失败: 响应解析异常");
        }

        boolean addProxySuccess = code == 0;
        boolean addProxyAlreadyExists = code != 0 && isAddProxyAlreadyExists(msg);

        if (!addProxySuccess && !addProxyAlreadyExists) {
            throw new ServiceException("调用 ZLM addStreamProxy 失败: " + msg);
        }

        Map<String, Object> result = new HashMap<>();
        result.put("apeId", apeId);
        result.put("stream", stream);
        result.put("playUrl", "ws://" + zlmServer.getHost() + ":" + zlmServer.getMedia_http_port() + "/" + zlmApp + "/" + stream + ".live.flv");
        result.put("zlmProxyKey", StringUtils.isBlank(zlmProxyKey) ? null : zlmProxyKey);
        result.put("addProxySuccess", addProxySuccess);
        result.put("addProxyAlreadyExists", addProxyAlreadyExists);
        result.put("protocol", "ws-flv");
        return result;
    }

    @Override
    public List<HDevice> selectDeviceList(HDevice device, Long userId) {
        device.setOrg_index(normalizeOrgIndex(device.getOrg_index()));
        List<HDevice> devices;
        SysUser user = userMapper.selectUserById(userId);
        SysDept dept = sysDeptMapper.selectDeptById(user.getDeptId());
        List<String> orgIndexs = null;
        if (!com.ruoyi.common.utils.SecurityUtils.isAdmin(userId)) {
            // 如果登录账号不为admin 账号
            if (device.getOrg_index() == null && !dept.getOrgIndex().equals("10")) {
                orgIndexs = sysDeptMapper.getOrgIndex(dept.getOrgIndex());
                orgIndexs.add(dept.getOrgIndex());
                String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                device.getParams().put("org_indexs", org_index);
            } else if (device.getOrg_index() != null && !dept.getOrgIndex().equals("10")) {
                orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                orgIndexs.add(device.getOrg_index());
                String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                device.getParams().put("org_indexs", org_index);
            } else if (device.getOrg_index() != null) {
                if (!device.getOrg_index().equals("10")) {
                    orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                    orgIndexs.add(device.getOrg_index());
                    String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                    device.getParams().put("org_indexs", org_index);
                }
            }
        } else if (!dept.getOrgIndex().equals("10")) {
            // 如果登录账号不为 hy 账号
            if (device.getOrg_index() == null) {
                orgIndexs = sysDeptMapper.getOrgIndex(dept.getOrgIndex());
                orgIndexs.add(dept.getOrgIndex());
                String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                device.getParams().put("org_indexs", org_index);
            } else {
                orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                orgIndexs.add(device.getOrg_index());
                String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                device.getParams().put("org_indexs", org_index);
            }
        } else {
            // 如果登录账号为 hy/admin 账号
            if (device.getOrg_index() != null) {
                if (!device.getOrg_index().equals("10")) {
                    orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                    orgIndexs.add(device.getOrg_index());
                    String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                    device.getParams().put("org_indexs", org_index);
                }
            } else {
                if (!dept.getOrgIndex().equals("10")) {
                    orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                    orgIndexs.add(device.getOrg_index());
                    String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                    device.getParams().put("org_indexs", org_index);
                } else if (!com.ruoyi.common.utils.SecurityUtils.isAdmin(userId)) {
                    orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                    orgIndexs.add(device.getOrg_index());
                    String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                    device.getParams().put("org_indexs", org_index);
                }
            }
        }

        PageDomain pageDomain = TableSupport.getPageDomain();
        PageHelper.startPage(pageDomain.getPageNum(), pageDomain.getPageSize(), pageDomain.getOrderBy());
        devices = hDeviceMapper.selectDeviceList(device);

        return devices;
    }

    @Override
    public Map<String, Object> getDeviceNum(Long userId) {
        SysUser user = userMapper.selectUserById(userId);
        SysDept dept = sysDeptMapper.selectDeptById(user.getDeptId());
        int deviceNum;
        int deviceEnableNum;
        if (com.ruoyi.common.utils.SecurityUtils.isAdmin(userId) || dept.getOrgIndex().equals("10")) {
            // 如果登录账号为 集团管理员和系统管理员 查询所有数量的设备
            deviceNum = hDeviceMapper.getDeviceNum();
            deviceEnableNum = hDeviceMapper.getDeviceEnableNum();
        } else {
            // 如果登录账号为 别的账号 根据大组织查询
            List<String> orgIndexs = sysDeptMapper.getOrgIndex(dept.getOrgIndex());
            orgIndexs.add(dept.getOrgIndex());
            String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
            HDevice device = new HDevice();
            device.getParams().put("org_indexs", org_index);
            deviceNum = hDeviceMapper.getDeviceNumByOrg(device);
            deviceEnableNum = hDeviceMapper.getDeviceEnableNumByOrg(device);
        }
        int deviceli = deviceNum - deviceEnableNum;
        Map<String, Object> map = new HashMap<>();
        map.put("deviceNum", deviceNum);
        map.put("deviceEnableNum", deviceEnableNum);
        map.put("deviceli", deviceli);
        return map;
    }

    @Override
    public List<HDevice> selectLDeviceList(HDevice device, Long userId) {
        device.setOrg_index(normalizeOrgIndex(device.getOrg_index()));
        List<HDevice> devices;
        SysUser user = userMapper.selectUserById(userId);
        SysDept dept = sysDeptMapper.selectDeptById(user.getDeptId());
        List<String> orgIndexs = null;
        if (!com.ruoyi.common.utils.SecurityUtils.isAdmin(userId)) {
            // 如果登录账号不为admin 账号
            if (device.getOrg_index() == null && !dept.getOrgIndex().equals("10")) {
                orgIndexs = sysDeptMapper.getOrgIndex(dept.getOrgIndex());
                orgIndexs.add(dept.getOrgIndex());
                String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                device.getParams().put("org_indexs", org_index);
            } else if (device.getOrg_index() != null && !dept.getOrgIndex().equals("10")) {
                orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                orgIndexs.add(device.getOrg_index());
                String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                device.getParams().put("org_indexs", org_index);
            } else if (device.getOrg_index() != null) {
                if (!device.getOrg_index().equals("10")) {
                    orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                    orgIndexs.add(device.getOrg_index());
                    String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                    device.getParams().put("org_indexs", org_index);
                }
            }
        } else if (!dept.getOrgIndex().equals("10")) {
            // 如果登录账号不为 hy 账号
            if (device.getOrg_index() == null) {
                orgIndexs = sysDeptMapper.getOrgIndex(dept.getOrgIndex());
                orgIndexs.add(dept.getOrgIndex());
                String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                device.getParams().put("org_indexs", org_index);
            } else {
                orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                orgIndexs.add(device.getOrg_index());
                String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                device.getParams().put("org_indexs", org_index);
            }
        } else {
            // 如果登录账号为 hy/admin 账号
            if (device.getOrg_index() != null) {
                if (!device.getOrg_index().equals("10")) {
                    orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                    orgIndexs.add(device.getOrg_index());
                    String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                    device.getParams().put("org_indexs", org_index);
                }
            } else {
                if (!dept.getOrgIndex().equals("10")) {
                    orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                    orgIndexs.add(device.getOrg_index());
                    String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                    device.getParams().put("org_indexs", org_index);
                } else if (!com.ruoyi.common.utils.SecurityUtils.isAdmin(userId)) {
                    orgIndexs = sysDeptMapper.getOrgIndex(device.getOrg_index());
                    orgIndexs.add(device.getOrg_index());
                    String[] org_index = orgIndexs.toArray(new String[orgIndexs.size()]);
                    device.getParams().put("org_indexs", org_index);
                }
            }
        }

        PageDomain pageDomain = TableSupport.getPageDomain();
        PageHelper.startPage(pageDomain.getPageNum(), pageDomain.getPageSize(), pageDomain.getOrderBy());
        devices = hDeviceMapper.selectLDeviceList(device);

        return devices;
    }

    @Override
    public int startMonitor(String apeId) {
        if (StringUtils.isBlank(apeId)) {
            throw new ServiceException("apeId 不能为空");
        }

        HDevice existedDevice = hDeviceMapper.selectDeviceByApeId(apeId);
        if (existedDevice == null) {
            throw new ServiceException("设备不存在: " + apeId);
        }

        String startAddProxyUrl = buildDirectAddProxyUrl(existedDevice);
        String startPlayUrl = buildDirectPlayUrl(existedDevice);
        // GB28181 国标设备不走 RTSP DIRECT 代理流：其播放地址由 ZLM 国标推流直接提供（play_url）
        boolean isGb28181Device = "gb28181".equalsIgnoreCase(existedDevice.getDevice_type());
        if (isDirectDevice(existedDevice) && !isGb28181Device) {
            Map<String, Object> directLiveInfo = getDirectLiveUrl(apeId);
            boolean addProxyAlreadyExists = Boolean.TRUE.equals(directLiveInfo.get("addProxyAlreadyExists"));
            if (addProxyAlreadyExists) {
                throw new ServiceException("设备监控已经启动过");
            }

            Object playUrlObj = directLiveInfo.get("playUrl");
            Object zlmProxyKeyObj = directLiveInfo.get("zlmProxyKey");
            if (playUrlObj != null) {
                startPlayUrl = String.valueOf(playUrlObj);
            }
            String zlmProxyKey = zlmProxyKeyObj == null ? null : String.valueOf(zlmProxyKeyObj);
            hDeviceMapper.updatePlayUrlByApeId(apeId, startPlayUrl);
            if (StringUtils.isNotBlank(zlmProxyKey)) {
                hDeviceMapper.updateZlmProxyKeyByApeId(apeId, zlmProxyKey);
            }
        }

        int updated = hDeviceMapper.updateMonitorStateByApeId(apeId, MONITOR_STATUS_RUNNING);
        if (updated <= 0) {
            throw new ServiceException("启动监控失败: " + apeId);
        }
        return updated;
    }

    @Override
    public int stopMonitor(String apeId) {
        if (StringUtils.isBlank(apeId)) {
            throw new ServiceException("apeId 不能为空");
        }

        HDevice existedDevice = hDeviceMapper.selectDeviceByApeId(apeId);
        if (existedDevice == null) {
            throw new ServiceException("设备不存在: " + apeId);
        }

        // GB28181 国标设备的 play_url 由 ZLM 国标推流长期提供，停止监控时保留；
        // 仅非 GB28181 的 RTSP DIRECT 代理设备在停止时删除代理流并清空临时代理地址与代理 key。
        boolean isGb28181Device = "gb28181".equalsIgnoreCase(existedDevice.getDevice_type());

        boolean directProxyDeleted = false;
        if (!isGb28181Device && isDirectDevice(existedDevice) && StringUtils.isNotBlank(existedDevice.getZlm_proxy_key())) {
            try {
                directProxyDeleted = deleteDirectStreamProxy(existedDevice);
            } catch (Exception e) {
                log.error("调用ZLM delStreamProxy失败, apeId={}, key={}", apeId, existedDevice.getZlm_proxy_key(), e);
            }
        }

        int updated = hDeviceMapper.updateMonitorStateByApeId(apeId, MONITOR_STATUS_STOPPED);
        if (updated <= 0) {
            throw new ServiceException("停止监控失败: " + apeId);
        }

        if (!isGb28181Device) {
            hDeviceMapper.updatePlayUrlByApeId(apeId, null);
            if (directProxyDeleted) {
                hDeviceMapper.updateZlmProxyKeyByApeId(apeId, null);
            }
        }
        return updated;
    }

    @Override
    public Map<String, Object> previewMonitor(String apeId) {
        if (StringUtils.isBlank(apeId)) {
            throw new ServiceException("apeId 不能为空");
        }

        HDevice device = hDeviceMapper.selectDeviceByApeId(apeId);
        if (device == null) {
            throw new ServiceException("设备不存在: " + apeId);
        }

        String previewAddProxyUrl = buildDirectAddProxyUrl(device);
        String previewPlayUrl = device.getPlay_url();
        if (StringUtils.isBlank(previewPlayUrl)) {
            previewPlayUrl = buildDirectPlayUrl(device);
        }
        if (StringUtils.isBlank(previewPlayUrl)) {
            previewPlayUrl = device.getDirect_source_url();
        }

        Map<String, Object> result = new HashMap<>();
        result.put("apeId", device.getApe_id());
        result.put("name", device.getName());
        result.put("streamSourceType", device.getStream_source_type());
        result.put("monitorStatus", device.getMonitor_status());
        result.put("directSourceUrl", device.getDirect_source_url());
        result.put("playUrl", previewPlayUrl);
        result.put("ipAddr", device.getIp_addr());
        result.put("port", device.getPort());
        result.put("supportedMonitorStatuses", new String[] {
            MONITOR_STATUS_RUNNING,
            MONITOR_STATUS_STOPPED,
            MONITOR_STATUS_STARTING,
            MONITOR_STATUS_STOPPING,
            MONITOR_STATUS_ERROR
        });
        return result;
    }

    private boolean isDirectDevice(HDevice device) {
        return device != null && STREAM_SOURCE_TYPE_DIRECT.equalsIgnoreCase(device.getStream_source_type());
    }

    private String normalizeOrgIndex(String orgIndex) {
        if (StringUtils.isBlank(orgIndex)) {
            return orgIndex;
        }

        String trimmed = orgIndex.trim();
        if (!trimmed.matches("\\d+")) {
            return orgIndex;
        }

        try {
            SysDept dept = sysDeptMapper.selectDeptById(Long.valueOf(trimmed));
            if (dept != null && StringUtils.isNotBlank(dept.getOrgIndex())) {
                return dept.getOrgIndex();
            }
        } catch (NumberFormatException ex) {
            log.warn("org_index 不是有效 deptId，按组织编码原样使用: {}", trimmed);
            return orgIndex;
        }

        return orgIndex;
    }

    private String sanitizeStreamName(String apeId) {
        String stream = STREAM_NAME_PATTERN.matcher(apeId == null ? "" : apeId).replaceAll("");
        if (StringUtils.isBlank(stream)) {
            return "cam" + System.currentTimeMillis();
        }
        return stream;
    }

    private String buildDirectAddProxyUrl(HDevice device) {
        if (device == null || !STREAM_SOURCE_TYPE_DIRECT.equalsIgnoreCase(device.getStream_source_type())
            || StringUtils.isBlank(device.getDirect_source_url())) {
            return "";
        }

        ZlmServer zlmServer = resolveEnabledZlmServer(device);
        if (zlmServer == null || StringUtils.isBlank(zlmServer.getHost()) || zlmServer.getApi_port() == null) {
            return "";
        }

        String zlmApp = StringUtils.isBlank(zlmServer.getApp()) ? DEFAULT_ZLM_APP : zlmServer.getApp().trim();
        String stream = sanitizeStreamName(device.getApe_id());
        return UriComponentsBuilder
            .fromUriString("http://" + zlmServer.getHost() + ":" + zlmServer.getApi_port() + "/index/api/addStreamProxy")
            .queryParam("vhost", "__defaultVhost__")
            .queryParam("app", zlmApp)
            .queryParam("stream", stream)
            .queryParam("url", device.getDirect_source_url())
            .queryParam("enable_mp4", 1)
            .queryParam("auto_close", 0)
            .queryParamIfPresent("secret", StringUtils.isNotBlank(zlmServer.getSecret())
                ? java.util.Optional.of(zlmServer.getSecret())
                : java.util.Optional.empty())
            .build(true)
            .toUriString();
    }

    private String buildDirectPlayUrl(HDevice device) {
        if (device == null || !STREAM_SOURCE_TYPE_DIRECT.equalsIgnoreCase(device.getStream_source_type())) {
            return "";
        }

        ZlmServer zlmServer = resolveEnabledZlmServer(device);
        if (zlmServer == null || StringUtils.isBlank(zlmServer.getHost()) || zlmServer.getMedia_http_port() == null) {
            return "";
        }

        String zlmApp = StringUtils.isBlank(zlmServer.getApp()) ? DEFAULT_ZLM_APP : zlmServer.getApp().trim();
        String stream = sanitizeStreamName(device.getApe_id());
        return "ws://" + zlmServer.getHost() + ":" + zlmServer.getMedia_http_port() + "/" + zlmApp + "/" + stream + ".live.flv";
    }

    private ZlmServer resolveEnabledZlmServer(HDevice device) {
        if (device == null) {
            return null;
        }
        Long zlmServerId = device.getZlm_server_id() == null ? DEFAULT_SERVER_ID : device.getZlm_server_id();
        return zlmServerMapper.selectEnabledById(zlmServerId);
    }

    private boolean deleteDirectStreamProxy(HDevice device) {
        ZlmServer zlmServer = resolveEnabledZlmServer(device);
        if (zlmServer == null || StringUtils.isBlank(zlmServer.getHost()) || zlmServer.getApi_port() == null) {
            log.error("删除代理流失败，设备未绑定可用ZLM服务器或配置缺失, apeId={}", device.getApe_id());
            return false;
        }

        String delProxyUrl = UriComponentsBuilder
            .fromUriString("http://" + zlmServer.getHost() + ":" + zlmServer.getApi_port() + "/index/api/delStreamProxy")
            .queryParam("key", device.getZlm_proxy_key())
            .queryParamIfPresent("secret", StringUtils.isNotBlank(zlmServer.getSecret())
                ? java.util.Optional.of(zlmServer.getSecret())
                : java.util.Optional.empty())
            .build(true)
            .toUriString();

        if (log.isDebugEnabled()) {
            log.debug("调用ZLM delStreamProxy, apeId={}, url={}", device.getApe_id(), maskSensitiveUrl(delProxyUrl));
        }

        ResponseEntity<String> response = restTemplate.getForEntity(delProxyUrl, String.class);
        String body = response.getBody();
        if (StringUtils.isBlank(body)) {
            log.error("调用 ZLM delStreamProxy 返回空响应, apeId={}", device.getApe_id());
            return false;
        }

        try {
            JsonNode root = OBJECT_MAPPER.readTree(body);
            int code = parseCode(root.path("code").asText());
            boolean flag = root.path("data").path("flag").asBoolean(false);
            if (code == 0 && flag) {
                return true;
            }
            String msg = root.path("msg").asText("");
            log.error("调用 ZLM delStreamProxy 失败, apeId={}, key={}, code={}, flag={}, msg={}",
                device.getApe_id(), device.getZlm_proxy_key(), code, flag, msg);
        } catch (Exception e) {
            log.error("调用 ZLM delStreamProxy 响应解析异常, apeId={}, key={}",
                device.getApe_id(), device.getZlm_proxy_key(), e);
        }
        return false;
    }

    private boolean isAddProxyAlreadyExists(String msg) {
        if (StringUtils.isBlank(msg)) {
            return false;
        }
        String lower = msg.toLowerCase();
        return lower.contains("already exists");
    }

    private int parseCode(Object code) {
        if (code instanceof Number) {
            return ((Number) code).intValue();
        }
        if (code == null) {
            return -1;
        }
        try {
            return Integer.parseInt(String.valueOf(code));
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private String maskSensitiveUrl(String url) {
        if (StringUtils.isBlank(url)) {
            return url;
        }
        return url.replaceAll("(?i)([?&](secret|token|access_token|auth|sign|signature)=)[^&]*", "$1***");
    }


    /**
     * 同步 ZLMediaKit 中的 GB28181 国标设备到本地设备表（幂等）。
     * 同一 gb_device_id 已存在则更新，不存在则新增。
     *
     * @return 本次处理的国标设备数量
     */
    @Override
    public int syncGbDevices() {
        ZlmServer zlmServer = zlmServerMapper.selectEnabledById(DEFAULT_SERVER_ID);
        if (zlmServer == null) {
            log.warn("[GB28181] 未找到启用的 ZLM 服务器，跳过国标设备同步");
            return 0;
        }
        List<GbDeviceDTO> gbDevices = fetchGbDevicesFromZlm(zlmServer);
        if (gbDevices == null) {
            log.warn("[GB28181] 获取国标设备失败，跳过本次同步（不更新在线/离线状态）");
            return 0;
        }
        int synced = 0;
        for (GbDeviceDTO gb : gbDevices) {
            if (StringUtils.isBlank(gb.getDeviceId())) {
                continue;
            }
            HDevice exist = hDeviceMapper.selectByGbDeviceId(gb.getDeviceId());
            if (exist == null) {
                // 不存在 -> 新增
                HDevice device = buildGbDevice(gb, zlmServer);
                try {
                    hDeviceMapper.insertDeviceCrud(device);
                    synced++;
                    log.info("[GB28181] 新增国标设备: {}", gb.getDeviceId());
                } catch (DuplicateKeyException e) {
                    // 唯一索引冲突：先按 gb_device_id 复查，确认是并发同步兜底场景
                    HDevice duplicated = hDeviceMapper.selectByGbDeviceId(gb.getDeviceId());
                    if (duplicated != null) {
                        // 确认冲突来自 gb_device_id 唯一键（并发同步兜底）→ 按更新处理
                        boolean changed = false;
                        if (StringUtils.isNotBlank(device.getPlay_url())
                                && !device.getPlay_url().equals(duplicated.getPlay_url())) {
                            duplicated.setPlay_url(device.getPlay_url());
                            changed = true;
                        }
                        if (StringUtils.isNotBlank(device.getIs_online())
                                && !device.getIs_online().equals(duplicated.getIs_online())) {
                            duplicated.setIs_online(device.getIs_online());
                            changed = true;
                        }
                        if (changed) {
                            hDeviceMapper.updateDevice(duplicated);
                        }
                        synced++;
                        log.warn("[GB28181] 并发插入冲突(唯一索引兜底)，按更新处理: {}", gb.getDeviceId());
                    } else {
                        // 冲突并非 gb_device_id 唯一键（如 ape_id 映射冲突），视为真实插入失败，不吞异常
                        log.error("[GB28181] 国标设备插入失败且未发现 gb_device_id 重复记录: {}, 原因: {}", gb.getDeviceId(), e.getMessage());
                    }
                }
            } else {
                // 存在 -> 更新（名称 / 在线状态 / 播放地址）
                boolean changed = false;
                if (StringUtils.isNotBlank(gb.getName()) && !gb.getName().equals(exist.getName())) {
                    exist.setName(gb.getName());
                    changed = true;
                }
                String online = normalizeGbStatus(gb.getStatus(), exist.getIs_online());
                if (StringUtils.isNotBlank(online) && !online.equals(exist.getIs_online())) {
                    exist.setIs_online(online);
                    changed = true;
                }
                if (StringUtils.isNotBlank(gb.getPlayUrl()) && !gb.getPlayUrl().equals(exist.getPlay_url())) {
                    exist.setPlay_url(gb.getPlayUrl());
                    changed = true;
                }
                if (changed) {
                    hDeviceMapper.updateDevice(exist);
                }
                synced++;
            }
        }
        // 本轮未返回的本地国标设备统一置为离线（在线 -> 离线状态同步）
        List<String> activeGbIds = new ArrayList<>();
        for (GbDeviceDTO gb : gbDevices) {
            if (StringUtils.isNotBlank(gb.getDeviceId())) {
                activeGbIds.add(gb.getDeviceId());
            }
        }
        int offlineUpdated = hDeviceMapper.updateGbDeviceOffline(activeGbIds);
        if (offlineUpdated > 0) {
            log.info("[GB28181] 国标设备离线状态同步完成，共更新 {} 台为离线", offlineUpdated);
        }
        log.info("[GB28181] 国标设备同步完成，共处理 {} 台", gbDevices.size());
        return synced;
    }

    /**
     * 从 ZLMediaKit 拉取 GB28181 国标设备列表（对接点）。
     * <p>
     * 当前 ZLMediaKit 的 GB28181(SIP) 接入由任务三（GB28181/ZLMediaKit 模块）负责，
     * 本方法为通用默认实现：先探测 ZLM 可用性，再从 getAllSession 会话中识别国标设备。
     * 待任务三提供实际调通的设备列表接口后，仅需调整本方法内的请求与字段映射。
     */
    private List<GbDeviceDTO> fetchGbDevicesFromZlm(ZlmServer zlmServer) {
        List<GbDeviceDTO> devices = new ArrayList<>();
        if (zlmServer == null || StringUtils.isBlank(zlmServer.getHost()) || zlmServer.getApi_port() == null) {
            log.warn("[GB28181] ZLM 服务器配置缺失，跳过国标设备同步（保留原状态）");
            return null;
        }
        String secret = StringUtils.isBlank(zlmServer.getSecret()) ? "" : zlmServer.getSecret();
        try {
            String url = UriComponentsBuilder
                    .fromUriString("http://" + zlmServer.getHost() + ":" + zlmServer.getApi_port()
                            + "/index/api/getAllSession")
                    .queryParam("secret", secret)
                    .build().toUriString();
            ResponseEntity<String> resp = restTemplate.getForEntity(url, String.class);
            if (resp.getBody() == null) {
                log.warn("[GB28181] ZLM getAllSession 响应为空，跳过本次同步（保留原状态）");
                return null;
            }
            JsonNode root = OBJECT_MAPPER.readTree(resp.getBody());
            if (root.path("code").asInt() != 0) {
                log.warn("[GB28181] ZLM getAllSession 返回异常: {}, 跳过本次同步（保留原状态）", root.path("msg").asText("unknown"));
                return null;
            }
            JsonNode data = root.path("data");
            if (!data.isArray()) {
                log.warn("[GB28181] ZLM getAllSession 返回 data 非数组，跳过本次同步（保留原状态）");
                return null;
            }
            for (JsonNode session : data) {
                String app = session.path("app").asText("");
                String schema = session.path("schema").asText("");
                String key = session.path("key").asText("");
                // GB28181 国标设备经 SIP 注册后推流，ZLM 以 RTP/TS 会话承载；此处为通用识别。
                boolean isGb = "rtp".equalsIgnoreCase(schema)
                        || app.toLowerCase().contains("gb")
                        || app.toLowerCase().contains("28181")
                        || key.toLowerCase().startsWith("340200");
                if (isGb && StringUtils.isNotBlank(key)) {
                    GbDeviceDTO dto = new GbDeviceDTO();
                    dto.setDeviceId(key);
                    dto.setName(key);
                    dto.setPlatformId(key);
                    dto.setStreamId(key);
                    dto.setStatus("online");
                    // 会话中携带播放地址（RTP/TS 会话一般含 play_url），用于前端预览；
                    // 任务三接通真实国标设备后，若字段不同仅需调整此处映射。
                    String playUrl = session.path("play_url").asText("");
                    dto.setPlayUrl(StringUtils.isNotBlank(playUrl) ? playUrl : null);
                    devices.add(dto);
                }
            }
        } catch (Exception e) {
            log.warn("[GB28181] 从 ZLM 获取国标设备失败: {}, 跳过本次同步（保留原状态）", e.getMessage());
            return null;
        }
        return devices;
    }

    /**
     * 规整国标设备在线状态：识别常见在线/离线取值；未知状态返回 fallback（更新时保留原状态、新增时默认离线）。
     */
    private String normalizeGbStatus(String status, String fallback) {
        if (StringUtils.isBlank(status)) {
            return fallback;
        }
        String s = status.trim().toLowerCase();
        switch (s) {
            case "online": case "1": case "true": case "registered": case "connected": case "up":
                return "1";
            case "offline": case "0": case "false": case "unregistered": case "disconnected": case "down":
                return "0";
            default:
                return fallback;
        }
    }

    /**
     * 将 GB28181 设备 DTO 转换为本地设备实体。
     */
    private HDevice buildGbDevice(GbDeviceDTO gb, ZlmServer zlmServer) {
        HDevice device = new HDevice();
        device.setApe_id(StringUtils.isNotBlank(gb.getStreamId()) ? gb.getStreamId() : gb.getDeviceId());
        device.setName(StringUtils.isNotBlank(gb.getName()) ? gb.getName() : gb.getDeviceId());
        device.setDevice_type("gb28181");
        device.setGb_device_id(gb.getDeviceId());
        device.setGb_platform_id(gb.getPlatformId());
        device.setStream_source_type("DIRECT");
        device.setResource_type("gb28181");
        device.setSub_type("gb28181");
        device.setIs_online(normalizeGbStatus(gb.getStatus(), "0"));
        device.setMonitor_status("STOPPED");
        device.setPlay_url(gb.getPlayUrl());
        device.setZlm_server_id(zlmServer.getId());
        device.setSva_server_id(1L);
        return device;
    }
}
