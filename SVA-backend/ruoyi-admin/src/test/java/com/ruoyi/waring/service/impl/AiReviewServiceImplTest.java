package com.ruoyi.waring.service.impl;

import com.ruoyi.common.config.RuoYiConfig;
import com.ruoyi.waring.domain.AiReviewTask;
import com.ruoyi.waring.domain.HWaring;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.test.util.ReflectionTestUtils;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class AiReviewServiceImplTest
{
    @TempDir
    Path profile;

    @Test
    @SuppressWarnings("unchecked")
    void deepSeekRequestInlinesPublicAlarmImageFromSharedUpload() throws Exception
    {
        Path image = profile.resolve("alarm/gbsleep001/test.jpg");
        Files.createDirectories(image.getParent());
        Files.write(image, new byte[] { 1, 2, 3, 4 });
        new RuoYiConfig().setProfile(profile.toString());

        AiReviewTask task = new AiReviewTask();
        task.setMediaUrl("http://43.161.237.211/alarm/gbsleep001/test.jpg");

        List<Map<String, Object>> messages = ReflectionTestUtils.invokeMethod(
            new AiReviewServiceImpl(), "buildOpenAiMessages", new HWaring(), task);

        List<Map<String, Object>> content = (List<Map<String, Object>>) messages.get(1).get("content");
        Map<String, Object> imageUrl = (Map<String, Object>) content.get(1).get("image_url");
        String value = String.valueOf(imageUrl.get("url"));
        assertTrue(value.startsWith("data:image/jpeg;base64,"));
        assertEquals("AQIDBA==", value.substring(value.indexOf(',') + 1));
    }
}
