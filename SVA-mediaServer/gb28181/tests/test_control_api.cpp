#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "json/json.h"
#include "core/SipResponse.h"
#include "service/GbControlApi.h"

using easy_sva::gb28181::DeviceCatalogStore;
using easy_sva::gb28181::DeviceChannel;
using easy_sva::gb28181::GbControlApi;
using easy_sva::gb28181::GbControlHttpResult;
using easy_sva::gb28181::GbLiveService;
using easy_sva::gb28181::GbMediaSession;
using easy_sva::gb28181::GbPlatformService;
using easy_sva::gb28181::GbSipConfig;
using easy_sva::gb28181::RegisteredDevice;
using easy_sva::gb28181::RegistrationStore;
using easy_sva::gb28181::SipMessage;
using easy_sva::gb28181::SipResponse;
using easy_sva::gb28181::ZlmApiClient;
using easy_sva::gb28181::ZlmHttpRequest;
using easy_sva::gb28181::ZlmHttpResponse;

namespace {

const char *deviceId = "34020000001320000001";
const char *channelId = "34020000001320000002";
int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

Json::Value parseJson(const std::string &body) {
    Json::Value value;
    Json::Reader reader;
    if (!reader.parse(body, value)) {
        ++failures;
        std::cerr << "FAIL: API response is not JSON: " << body << std::endl;
    }
    return value;
}

struct Fixture {
    GbSipConfig config;
    RegistrationStore::Ptr registrations;
    DeviceCatalogStore::Ptr catalogs;
    GbPlatformService::Ptr platform;
    GbLiveService::Ptr live;
    GbControlApi api;
    std::vector<std::string> outbound;
    std::vector<ZlmHttpRequest> zlmRequests;

    Fixture()
        : config(configWithSecret()),
          registrations(new RegistrationStore()),
          catalogs(new DeviceCatalogStore()),
          platform(new GbPlatformService(config, registrations, catalogs)),
          live(new GbLiveService(
              config, registrations, catalogs, GbLiveService::Clock(),
              [this](const ZlmHttpRequest &request,
                     const ZlmApiClient::HttpCompletion &completion) {
                  zlmRequests.push_back(request);
                  ZlmHttpResponse response;
                  response.statusCode = 200;
                  response.body = request.url.find("openRtpServer") != std::string::npos
                      ? "{\"code\":0,\"port\":30000}"
                      : "{\"code\":0,\"hit\":1}";
                  completion(response);
              })),
          api(config, registrations, catalogs, platform, live) {
        RegisteredDevice device;
        device.deviceId = deviceId;
        device.transport = "UDP";
        device.peerIp = "192.0.2.20";
        device.peerPort = 5060;
        device.userAgent = "GB-device/1.0";
        device.registeredAt = 1000;
        device.lastRegisterAt = 1000;
        device.lastHeartbeatAt = 1010;
        device.expiresAt = 4600;
        device.online = true;
        device.sender = [this](const std::string &wire) {
            outbound.push_back(wire);
            return true;
        };
        registrations->upsert(device);

        DeviceChannel channel;
        channel.deviceId = channelId;
        channel.name = "front gate";
        channel.status = "ON";
        catalogs->replace(device.deviceId,
                          std::vector<DeviceChannel>(1, channel), 1015);
    }

    static GbSipConfig configWithSecret() {
        GbSipConfig value;
        value.apiSecret = "control-secret";
        value.advertisedIp = "203.0.113.10";
        value.rtpAdvertisedIp = "198.51.100.10";
        value.zlmApiSecret = "test-secret";
        return value;
    }

    SipMessage lastRequest(const std::string &method) const {
        for (std::vector<std::string>::const_reverse_iterator it = outbound.rbegin();
             it != outbound.rend(); ++it) {
            SipMessage message;
            if (SipMessage::parse(*it, message, nullptr, nullptr) &&
                message.isRequest() && message.method() == method) {
                return message;
            }
        }
        return SipMessage();
    }
};

std::string validAnswer(const std::string &ssrc) {
    return "v=0\r\n"
           "o=34020000001320000001 0 0 IN IP4 192.0.2.20\r\n"
           "s=Play\r\n"
           "c=IN IP4 192.0.2.20\r\n"
           "t=0 0\r\n"
           "m=video 62000 RTP/AVP 96\r\n"
           "a=sendonly\r\n"
           "a=rtpmap:96 PS/90000\r\n"
           "y=" + ssrc + "\r\n";
}

SipMessage finalResponse(const SipMessage &request,
                         const std::string &body = std::string()) {
    SipMessage response = SipResponse::fromRequest(
        request, 200, "OK", "device-tag", body,
        body.empty() ? std::string() : "application/sdp");
    if (request.method() == "INVITE") {
        response.addHeader("Contact",
            "<sip:34020000001320000001@192.0.2.20:5060>");
    }
    return response;
}

void testHealthAndAuthorization() {
    Fixture fixture;
    GbControlHttpResult result = fixture.api.dispatch(
        "GET", "/gb28181/api/health", "");
    Json::Value json = parseJson(result.body);
    expect(result.statusCode == 200 && json["code"].asInt() == 0 &&
           json["data"]["status"].asString() == "ok",
           "health endpoint is available without a secret");

    result = fixture.api.dispatch("GET", "/gb28181/api/devices", "");
    expect(result.statusCode == 401,
           "management data rejects a missing bearer secret");
    result = fixture.api.dispatch(
        "GET", "/gb28181/api/devices", "Bearer wrong");
    expect(result.statusCode == 401,
           "management data rejects an incorrect bearer secret");
}

void testReadEndpoints() {
    Fixture fixture;
    const std::string authorization = "Bearer control-secret";
    GbControlHttpResult result = fixture.api.dispatch(
        "GET", "/gb28181/api/devices", authorization);
    Json::Value json = parseJson(result.body);
    expect(result.statusCode == 200 && json["data"].size() == 1 &&
           json["data"][0]["device_id"].asString() ==
               "34020000001320000001" &&
           json["data"][0]["online"].asBool(),
           "devices endpoint exposes current registration and heartbeat state");
    expect(result.body.find("control-secret") == std::string::npos,
           "API secret is never serialized in device output");

    GbControlApi::Parameters parameters;
    result = fixture.api.dispatch(
        "GET", "/gb28181/api/catalog", authorization, parameters);
    expect(result.statusCode == 400,
           "catalog endpoint requires a device_id");
    parameters["device_id"] = "34020000001320000001";
    result = fixture.api.dispatch(
        "GET", "/gb28181/api/catalog", authorization, parameters);
    json = parseJson(result.body);
    expect(result.statusCode == 200 && json["data"].size() == 1 &&
           json["data"][0]["channel_id"].asString() ==
               "34020000001320000002" &&
           json["data"][0]["name"].asString() == "front gate",
           "catalog endpoint exposes channels for the selected device");

    result = fixture.api.dispatch(
        "GET", "/gb28181/api/sessions", authorization);
    json = parseJson(result.body);
    expect(result.statusCode == 200 && json["data"].empty(),
           "sessions endpoint returns an empty JSON array before playback");
}

void testMethodAndRouteErrors() {
    Fixture fixture;
    GbControlHttpResult result = fixture.api.dispatch(
        "DELETE", "/gb28181/api/devices", "Bearer control-secret");
    expect(result.statusCode == 405,
           "management API rejects unsupported HTTP methods");
    result = fixture.api.dispatch(
        "GET", "/gb28181/api/missing", "Bearer control-secret");
    expect(result.statusCode == 404,
           "unknown management route returns 404");
}

void testCatalogAndLiveActions() {
    Fixture fixture;
    const std::string authorization = "Bearer control-secret";
    GbControlApi::Parameters parameters;
    GbControlHttpResult result = fixture.api.dispatch(
        "POST", "/gb28181/api/catalog/query", authorization, parameters);
    expect(result.statusCode == 400,
           "Catalog action requires device_id");

    parameters["device_id"] = deviceId;
    result = fixture.api.dispatch(
        "POST", "/gb28181/api/catalog/query", authorization, parameters);
    Json::Value json = parseJson(result.body);
    expect(result.statusCode == 202 &&
           json["data"]["state"].asString() == "pending" &&
           fixture.lastRequest("MESSAGE").method() == "MESSAGE",
           "Catalog action returns a trackable command and sends SIP MESSAGE");
    const std::string commandId = json["data"]["command_id"].asString();
    GbControlApi::Parameters commandParameters;
    commandParameters["command_id"] = commandId;
    result = fixture.api.dispatch(
        "GET", "/gb28181/api/commands", authorization, commandParameters);
    json = parseJson(result.body);
    expect(result.statusCode == 200 &&
           json["data"]["command_id"].asString() == commandId,
           "command endpoint retrieves an individual Catalog command");

    parameters["channel_id"] = channelId;
    result = fixture.api.dispatch(
        "POST", "/gb28181/api/live/start", authorization, parameters);
    json = parseJson(result.body);
    const std::string sessionId = json["data"]["session_id"].asString();
    expect(result.statusCode == 202 && !sessionId.empty() &&
           json["data"]["state"].asString() == "inviting" &&
           fixture.lastRequest("INVITE").method() == "INVITE",
           "live-start action allocates RTP and sends INVITE");

    GbMediaSession session;
    fixture.live->findSession(sessionId, session);
    const SipMessage invite = fixture.lastRequest("INVITE");
    fixture.live->handleResponse(finalResponse(invite, validAnswer(session.ssrc)));
    GbControlApi::Parameters stopParameters;
    stopParameters["session_id"] = sessionId;
    result = fixture.api.dispatch(
        "POST", "/gb28181/api/live/stop", authorization, stopParameters);
    json = parseJson(result.body);
    expect(result.statusCode == 202 &&
           json["data"]["state"].asString() == "stopping" &&
           fixture.lastRequest("BYE").method() == "BYE",
           "live-stop action sends BYE and returns the stopping session");
    fixture.live->handleResponse(finalResponse(fixture.lastRequest("BYE")));
}

void testActionFailures() {
    Fixture fixture;
    GbControlApi::Parameters parameters;
    parameters["device_id"] = "34020000001320000099";
    parameters["channel_id"] = channelId;
    GbControlHttpResult result = fixture.api.dispatch(
        "POST", "/gb28181/api/live/start",
        "Bearer control-secret", parameters);
    expect(result.statusCode == 404,
           "live-start reports an unregistered device as not found");
    result = fixture.api.dispatch(
        "POST", "/gb28181/api/live/start", "", parameters);
    expect(result.statusCode == 401,
           "control actions require bearer authorization");
}

} // namespace

int main() {
    testHealthAndAuthorization();
    testReadEndpoints();
    testMethodAndRouteErrors();
    testCatalogAndLiveActions();
    testActionFailures();

    if (failures != 0) {
        std::cerr << failures << " GB28181 control-API test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 control-API tests passed" << std::endl;
    return EXIT_SUCCESS;
}
