package com.ruoyi.web.controller.algorithm;

import com.ruoyi.common.core.domain.AjaxResult;
import com.ruoyi.waring.domain.SvaServer;
import com.ruoyi.waring.mapper.SvaServerMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.HttpEntity;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.web.client.RestTemplate;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.*;
import static org.springframework.http.ResponseEntity.ok;

@ExtendWith(MockitoExtension.class)
class AlgorithmTuningControllerTest
{
    private static final String ENGINE = "http://analyzer:9993/api/control/update-algorithm-config";
    @Mock private RestTemplate restTemplate;
    @Mock private SvaServerMapper svaServerMapper;
    private AlgorithmTuningController controller;

    @BeforeEach
    void setUp()
    {
        controller = new AlgorithmTuningController();
        ReflectionTestUtils.setField(controller, "restTemplate", restTemplate);
        ReflectionTestUtils.setField(controller, "svaServerMapper", svaServerMapper);
    }

    private void engineReply(String body)
    {
        SvaServer server = new SvaServer();
        server.setHost("analyzer");
        server.setAnalyzer_port(9993);
        when(svaServerMapper.selectEnabledById(1L)).thenReturn(server);
        when(restTemplate.postForEntity(eq(ENGINE), any(HttpEntity.class), eq(String.class)))
            .thenReturn(ok(body));
    }

    private AlgorithmTuningConfig preset(String control)
    {
        AlgorithmTuningConfig config = new AlgorithmTuningConfig();
        config.setPreset("MEDIUM");
        config.setControlCode(control);
        return config;
    }

    @SuppressWarnings("unchecked")
    private Map<String, Object> state()
    {
        return (Map<String, Object>) controller.current().get("data");
    }

    @Test
    void restartDoesNotClaimDefaultIsEngineState()
    {
        assertNull(state().get("current"));
        assertNull(state().get("lastApplied"));
        assertNotNull(state().get("defaultConfig"));
        assertEquals("UNVERIFIED", state().get("configurationState"));
        verifyNoInteractions(restTemplate, svaServerMapper);
    }

    @Test
    void legacySuccessWithoutAppliedControlsIsNotAccepted()
    {
        engineReply("{\"code\":1000,\"msg\":\"global algorithm detection thresholds updated\"}");
        assertEquals(500, controller.update(preset("*")).get("code"));
        assertNull(state().get("lastApplied"));
    }

    @Test
    void emptyAppliedControlsIsNotSuccess()
    {
        engineReply("{\"code\":1000,\"updatedControls\":[],\"globalThresholdsUpdated\":true}");
        assertEquals(500, controller.update(preset("*")).get("code"));
    }

    @Test
    void engineFailureDoesNotSaveRequestedConfiguration()
    {
        engineReply("{\"code\":0,\"msg\":\"no matching control found to update\"}");
        assertEquals(500, controller.update(preset("*")).get("code"));
        assertNull(state().get("lastApplied"));
    }

    @Test
    void broadcastReportsActualControlsAndKeepsOnlyHistoricalReceipt()
    {
        engineReply("{\"code\":1000,\"updatedControls\":[\"task-a\",\"task-b\"],\"globalThresholdsUpdated\":true}");
        AjaxResult result = controller.update(preset("*"));
        assertEquals(200, result.get("code"));
        assertEquals(java.util.List.of("task-a", "task-b"), result.get("updatedControls"));
        assertEquals(true, result.get("globalThresholdsUpdated"));
        assertNotNull(state().get("lastApplied"));
        assertNull(state().get("current"));
        assertEquals("UNVERIFIED", state().get("configurationState"));
    }

    @Test
    @SuppressWarnings("unchecked")
    void targetedPresetDoesNotSendSharedModelThresholds()
    {
        engineReply("{\"code\":1000,\"updatedControls\":[\"task-a\"],\"globalThresholdsUpdated\":false}");
        assertEquals(200, controller.update(preset("task-a")).get("code"));
        ArgumentCaptor<HttpEntity> entity = ArgumentCaptor.forClass(HttpEntity.class);
        verify(restTemplate).postForEntity(eq(ENGINE), entity.capture(), eq(String.class));
        Map<String, Object> payload = (Map<String, Object>) entity.getValue().getBody();
        assertEquals("task-a", payload.get("controlCode"));
        assertFalse(payload.containsKey("detectionConfidence"));
        assertFalse(payload.containsKey("nmsThreshold"));
        assertEquals(15.0, payload.get("confirmWindowSec"));
    }

    @Test
    void explicitSharedThresholdForSingleControlIsRejectedBeforeSending()
    {
        AlgorithmTuningConfig config = new AlgorithmTuningConfig();
        config.setPreset("CUSTOM");
        config.setControlCode("task-a");
        config.setDetectionConfidence(0.4);
        assertEquals(500, controller.update(config).get("code"));
        verifyNoInteractions(restTemplate, svaServerMapper);
    }

    @Test
    void outOfRangeValueIsRejectedBeforeSending()
    {
        AlgorithmTuningConfig config = new AlgorithmTuningConfig();
        config.setPreset("CUSTOM");
        config.setSleepPositiveRatio(1.5);
        assertEquals(500, controller.update(config).get("code"));
        verifyNoInteractions(restTemplate, svaServerMapper);
    }

    @Test
    void emptyCustomRequestIsRejected()
    {
        assertEquals(500, controller.update(new AlgorithmTuningConfig()).get("code"));
        verifyNoInteractions(restTemplate, svaServerMapper);
    }

    @Test
    void mismatchedTaskReceiptIsRejected()
    {
        engineReply("{\"code\":1000,\"updatedControls\":[\"task-b\"],\"globalThresholdsUpdated\":false}");
        assertEquals(500, controller.update(preset("task-a")).get("code"));
        assertNull(state().get("lastApplied"));
    }
}
