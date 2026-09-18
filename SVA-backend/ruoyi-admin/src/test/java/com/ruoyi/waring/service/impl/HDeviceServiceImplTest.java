package com.ruoyi.waring.service.impl;

import com.ruoyi.common.exception.ServiceException;
import com.ruoyi.waring.domain.HDevice;
import com.ruoyi.waring.domain.ZlmServer;
import com.ruoyi.waring.mapper.HDeviceMapper;
import com.ruoyi.waring.mapper.ZlmServerMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.web.client.ResourceAccessException;
import org.springframework.web.client.RestTemplate;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;
import static org.springframework.http.ResponseEntity.ok;

@ExtendWith(MockitoExtension.class)
class HDeviceServiceImplTest
{
    @Mock
    private HDeviceMapper hDeviceMapper;

    @Mock
    private ZlmServerMapper zlmServerMapper;

    @Mock
    private RestTemplate restTemplate;

    private HDeviceServiceImpl service;

    @BeforeEach
    void setUp()
    {
        service = new HDeviceServiceImpl();
        ReflectionTestUtils.setField(service, "hDeviceMapper", hDeviceMapper);
        ReflectionTestUtils.setField(service, "zlmServerMapper", zlmServerMapper);
        ReflectionTestUtils.setField(service, "restTemplate", restTemplate);
        setConfigurationField("gbApiPort", 19080);
        setConfigurationField("gbPlatformId", "34020000002000000001-test");
    }

    @Test
    void syncUsesConfiguredEndpointAndPlatformId()
    {
        when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", 9992));
        when(restTemplate.getForEntity(
            "http://10.0.0.8:19080/gb28181/api/devices", String.class))
            .thenReturn(ok("{\"code\":0,\"data\":[{\"device_id\":\"34020000001320000001\",\"user_agent\":\"cam-1\",\"online\":true}] }"));
        when(restTemplate.getForEntity(
            "http://10.0.0.8:19080/gb28181/api/sessions", String.class))
            .thenReturn(ok("{\"code\":0,\"data\":[{\"device_id\":\"34020000001320000001\",\"state\":\"streaming\",\"stream_id\":\"stream-1\"}] }"));
        when(hDeviceMapper.selectByGbDeviceId("34020000001320000001")).thenReturn(null);
        when(hDeviceMapper.insertDeviceCrud(any(HDevice.class))).thenReturn(1);
        when(hDeviceMapper.updateGbDeviceOffline(anyList())).thenReturn(0);

        assertEquals(1, service.syncGbDevices());

        ArgumentCaptor<HDevice> captor = ArgumentCaptor.forClass(HDevice.class);
        verify(hDeviceMapper).insertDeviceCrud(captor.capture());
        HDevice inserted = captor.getValue();
        assertEquals("gb28181", inserted.getDevice_type());
        assertEquals("34020000002000000001-test", inserted.getGb_platform_id());
        assertEquals("ws://10.0.0.8:9992/rtp/stream-1.live.flv", inserted.getPlay_url());
    }

    @Test
    void missingMediaPortLeavesPlayUrlEmpty()
    {
        when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", null));
        when(restTemplate.getForEntity(
            "http://10.0.0.8:19080/gb28181/api/devices", String.class))
            .thenReturn(ok("{\"code\":0,\"data\":[{\"device_id\":\"34020000001320000001\",\"online\":true}] }"));
        when(restTemplate.getForEntity(
            "http://10.0.0.8:19080/gb28181/api/sessions", String.class))
            .thenReturn(ok("{\"code\":0,\"data\":[{\"device_id\":\"34020000001320000001\",\"state\":\"streaming\",\"stream_id\":\"stream-1\"}] }"));
        when(hDeviceMapper.selectByGbDeviceId("34020000001320000001")).thenReturn(null);
        when(hDeviceMapper.insertDeviceCrud(any(HDevice.class))).thenReturn(1);
        when(hDeviceMapper.updateGbDeviceOffline(anyList())).thenReturn(0);

        assertEquals(1, service.syncGbDevices());

        ArgumentCaptor<HDevice> captor = ArgumentCaptor.forClass(HDevice.class);
        verify(hDeviceMapper).insertDeviceCrud(captor.capture());
        assertTrue(captor.getValue().getPlay_url() == null || captor.getValue().getPlay_url().isEmpty());
    }

    @Test
    void invalidPortReportsConfigurationKey()
    {
        setConfigurationField("gbApiPort", 0);
        when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", 9992));

        ServiceException error = assertThrows(ServiceException.class, service::syncGbDevices);

        assertTrue(error.getMessage().contains("gb28181.api-port"));
        verifyNoInteractions(restTemplate);
    }

    @Test
    void blankPlatformIdReportsConfigurationKey()
    {
        setConfigurationField("gbPlatformId", " ");
        when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", 9992));

        ServiceException error = assertThrows(ServiceException.class, service::syncGbDevices);

        assertTrue(error.getMessage().contains("gb28181.platform-id"));
        verifyNoInteractions(restTemplate);
    }

    @Test
    void controlApiFailureDoesNotReconcileExistingDevicesOffline()
    {
        when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", 9992));
        when(restTemplate.getForEntity(
            "http://10.0.0.8:19080/gb28181/api/devices", String.class))
            .thenThrow(new ResourceAccessException("connection refused"));

        assertEquals(0, service.syncGbDevices());

        verify(hDeviceMapper, never()).updateGbDeviceOffline(anyList());
        verify(hDeviceMapper, never()).updateDevice(any(HDevice.class));
    }

    @Test
    void gbPreviewWithoutStoredPlayUrlBuildsConfiguredMediaUrl()
    {
        HDevice device = new HDevice();
        device.setApe_id("gb-1");
        device.setName("GB camera");
        device.setDevice_type("gb28181");
        device.setStream_source_type("DIRECT");
        device.setDirect_source_url("gb28181://10.0.0.8/live/gb-1");
        device.setPlay_url(null);
        device.setMonitor_status("STOPPED");
        when(hDeviceMapper.selectDeviceByApeId("gb-1")).thenReturn(device);
        when(zlmServerMapper.selectEnabledById(1L))
            .thenReturn(zlmServer(1L, "10.0.0.8", 9992));

        Map<String, Object> result = service.previewMonitor("gb-1");

        assertNotNull(result);
        assertEquals("ws://10.0.0.8:9992/live/gb-1.live.flv", result.get("playUrl"));
    }

    @Test
    void rtspPreviewBuildsConfiguredMediaUrl()
    {
        HDevice device = new HDevice();
        device.setApe_id("cam-1");
        device.setName("RTSP camera");
        device.setDevice_type("rtsp");
        device.setStream_source_type("DIRECT");
        device.setDirect_source_url("rtsp://10.0.0.8:9994/live/cam-1");
        device.setPlay_url(null);
        device.setMonitor_status("STOPPED");
        when(hDeviceMapper.selectDeviceByApeId("cam-1")).thenReturn(device);
        when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", 9992));

        Map<String, Object> result = service.previewMonitor("cam-1");

        assertEquals("ws://10.0.0.8:9992/live/cam-1.live.flv", result.get("playUrl"));
        assertFalse(result.containsKey("previewAddProxyUrl"));
    }

    private void setConfigurationField(String fieldName, Object value)
    {
        ReflectionTestUtils.setField(service, fieldName, value);
    }

    private ZlmServer zlmServer(Long id, String host, Integer mediaHttpPort)
    {
        ZlmServer server = new ZlmServer();
        server.setId(id);
        server.setApp("live");
        server.setHost(host);
        server.setMedia_http_port(mediaHttpPort);
        return server;
    }
}
