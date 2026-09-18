package com.ruoyi.waring.service;

import java.net.URI;

/**
 * 将容器内部生成的媒体地址转换为浏览器可访问的公网地址。
 */
public final class PublicMediaUrl
{
    private PublicMediaUrl()
    {
    }

    public static String resolve(String base, String original)
    {
        if (base == null || base.trim().isEmpty() || original == null || original.isEmpty())
        {
            return original;
        }

        URI origin = URI.create(base.trim());
        if (!("http".equals(origin.getScheme()) || "https".equals(origin.getScheme()))
                || origin.getHost() == null
                || origin.getUserInfo() != null
                || origin.getQuery() != null
                || origin.getFragment() != null
                || (!origin.getPath().isEmpty() && !"/".equals(origin.getPath())))
        {
            throw new IllegalArgumentException("easysva.public-base-url 必须是 HTTP/HTTPS 站点根地址");
        }

        URI url = URI.create(original);
        String path = url.getRawPath();
        if (path == null)
        {
            path = "";
        }
        if (!path.startsWith("/"))
        {
            path = "/" + path;
        }

        boolean stream = "ws".equals(url.getScheme()) || "wss".equals(url.getScheme());
        String scheme = stream ? ("https".equals(origin.getScheme()) ? "wss" : "ws") : origin.getScheme();
        String query = url.getRawQuery() == null ? "" : "?" + url.getRawQuery();
        return scheme + "://" + origin.getRawAuthority() + (stream ? "/media" : "") + path + query;
    }
}
