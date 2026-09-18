package com.ruoyi.waring.service;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

class PublicMediaUrlTest
{
    @Test
    void keepsOriginalWhenPublicBaseIsEmpty()
    {
        assertEquals("ws://media:9992/live/camera.live.flv",
            PublicMediaUrl.resolve("", "ws://media:9992/live/camera.live.flv"));
    }

    @Test
    void rewritesHttpMediaUrlToPublicOrigin()
    {
        assertEquals("http://43.161.237.211/alarm/camera.jpg?x=1",
            PublicMediaUrl.resolve("http://43.161.237.211", "http://media:9992/alarm/camera.jpg?x=1"));
    }

    @Test
    void rewritesWebSocketMediaUrlThroughMediaProxy()
    {
        assertEquals("wss://demo.example/media/live/camera.live.flv",
            PublicMediaUrl.resolve("https://demo.example/", "ws://media:9992/live/camera.live.flv"));
    }

    @Test
    void rejectsBaseUrlWithPath()
    {
        assertThrows(IllegalArgumentException.class,
            () -> PublicMediaUrl.resolve("https://demo.example/app", "http://media/alarm/a.jpg"));
    }
}
