#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "json/json.h"
#include "service/GbControlApi.h"

using easy_sva::gb28181::DeviceCatalogStore;
using easy_sva::gb28181::DeviceChannel;
using easy_sva::gb28181::GbControlApi;
using easy_sva::gb28181::GbControlHttpResult;
using easy_sva::gb28181::GbLiveService;
using easy_sva::gb28181::GbSipConfig;
using easy_sva::gb28181::RegisteredDevice;
using easy_sva::gb28181::RegistrationStore;

namespace {

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
    GbLiveService::Ptr live;
    GbControlApi api;

    Fixture()
        : registrations(new RegistrationStore()),
          catalogs(new DeviceCatalogStore()),
          live(new GbLiveService(config, registrations, catalogs)),
          api(configWithSecret(), registrations, catalogs, live) {
        RegisteredDevice device;
        device.deviceId = "34020000001320000001";
        device.transport = "UDP";
        device.peerIp = "192.0.2.20";
        device.peerPort = 5060;
        device.userAgent = "GB-device/1.0";
        device.registeredAt = 1000;
        device.lastRegisterAt = 1000;
        device.lastHeartbeatAt = 1010;
        device.expiresAt = 4600;
        device.online = true;
        registrations->upsert(device);

        DeviceChannel channel;
        channel.deviceId = "34020000001320000002";
        channel.name = "front gate";
        channel.status = "ON";
        catalogs->replace(device.deviceId,
                          std::vector<DeviceChannel>(1, channel), 1015);
    }

    static GbSipConfig configWithSecret() {
        GbSipConfig value;
        value.apiSecret = "control-secret";
        return value;
    }
};

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
        "POST", "/gb28181/api/devices", "Bearer control-secret");
    expect(result.statusCode == 405,
           "read-only management API rejects non-GET methods");
    result = fixture.api.dispatch(
        "GET", "/gb28181/api/missing", "Bearer control-secret");
    expect(result.statusCode == 404,
           "unknown management route returns 404");
}

} // namespace

int main() {
    testHealthAndAuthorization();
    testReadEndpoints();
    testMethodAndRouteErrors();

    if (failures != 0) {
        std::cerr << failures << " GB28181 control-API test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 control-API tests passed" << std::endl;
    return EXIT_SUCCESS;
}
