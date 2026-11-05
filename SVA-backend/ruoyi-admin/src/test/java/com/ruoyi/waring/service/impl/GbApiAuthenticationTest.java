package com.ruoyi.waring.service.impl;

import org.junit.jupiter.api.Test;
import org.springframework.http.HttpMethod;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.test.web.client.MockRestServiceServer;
import org.springframework.web.client.RestTemplate;

import static org.springframework.http.HttpHeaders.AUTHORIZATION;
import static org.springframework.http.MediaType.APPLICATION_JSON;
import static org.springframework.test.web.client.match.MockRestRequestMatchers.header;
import static org.springframework.test.web.client.match.MockRestRequestMatchers.method;
import static org.springframework.test.web.client.match.MockRestRequestMatchers.requestTo;
import static org.springframework.test.web.client.response.MockRestResponseCreators.withSuccess;

class GbApiAuthenticationTest
{
    @Test
    void sendsConfiguredBearerToken()
    {
        RestTemplate restTemplate = new RestTemplate();
        MockRestServiceServer server = MockRestServiceServer.bindTo(restTemplate).build();
        HDeviceServiceImpl service = service(restTemplate, "secret-for-test");

        server.expect(requestTo("http://media:18080/gb28181/api/devices"))
            .andExpect(method(HttpMethod.GET))
            .andExpect(header(AUTHORIZATION, "Bearer secret-for-test"))
            .andRespond(withSuccess("{\"code\":0,\"data\":[]}", APPLICATION_JSON));

        ReflectionTestUtils.invokeMethod(service, "getGbApi", "http://media:18080/gb28181/api/devices");
        server.verify();
    }

    @Test
    void allowsLocalApiWithoutConfiguredToken()
    {
        RestTemplate restTemplate = new RestTemplate();
        MockRestServiceServer server = MockRestServiceServer.bindTo(restTemplate).build();
        HDeviceServiceImpl service = service(restTemplate, "");

        server.expect(requestTo("http://media:18080/gb28181/api/devices"))
            .andExpect(method(HttpMethod.GET))
            .andRespond(withSuccess("{\"code\":0,\"data\":[]}", APPLICATION_JSON));

        ReflectionTestUtils.invokeMethod(service, "getGbApi", "http://media:18080/gb28181/api/devices");
        server.verify();
    }

    private HDeviceServiceImpl service(RestTemplate restTemplate, String secret)
    {
        HDeviceServiceImpl service = new HDeviceServiceImpl();
        ReflectionTestUtils.setField(service, "restTemplate", restTemplate);
        ReflectionTestUtils.setField(service, "gbApiSecret", secret);
        return service;
    }
}
