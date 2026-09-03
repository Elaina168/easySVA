#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "core/SipResponse.h"
#include "service/DeviceCatalogStore.h"
#include "service/GbPlatformService.h"
#include "service/GbSipRequestProcessor.h"
#include "service/RegistrationStore.h"

using easy_sva::gb28181::DeviceCatalogStore;
using easy_sva::gb28181::GbPlatformService;
using easy_sva::gb28181::GbSipConfig;
using easy_sva::gb28181::GbSipRequestProcessor;
using easy_sva::gb28181::PlatformCommand;
using easy_sva::gb28181::PlatformCommandFailed;
using easy_sva::gb28181::PlatformCommandPending;
using easy_sva::gb28181::PlatformCommandSucceeded;
using easy_sva::gb28181::PlatformCommandTimedOut;
using easy_sva::gb28181::RegisteredDevice;
using easy_sva::gb28181::RegistrationStore;
using easy_sva::gb28181::SipMessage;
using easy_sva::gb28181::SipPeer;
using easy_sva::gb28181::SipResponse;

namespace {

const char *deviceId = "34020000001320000001";
int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

struct Fixture {
    uint64_t now;
    GbSipConfig config;
    RegistrationStore::Ptr registrations;
    DeviceCatalogStore::Ptr catalogs;
    GbPlatformService::Ptr platform;
    std::shared_ptr<GbSipRequestProcessor> processor;
    std::string outbound;
    bool sendSucceeds;

    Fixture()
        : now(1000), registrations(new RegistrationStore()),
          catalogs(new DeviceCatalogStore()), sendSucceeds(true) {
        config.authRequired = false;
        config.advertisedIp = "203.0.113.10";
        config.transactionTimeoutSeconds = 5;
        GbPlatformService::Clock clock = [this]() { return now; };
        platform.reset(new GbPlatformService(config, registrations, catalogs, clock));
        processor.reset(new GbSipRequestProcessor(config));
        processor->setResponseHandler([this](const SipMessage &response, const SipPeer &) {
            platform->handleResponse(response);
        });
    }

    void addDevice(bool online = true) {
        RegisteredDevice device;
        device.deviceId = deviceId;
        device.transport = "UDP";
        device.peerIp = "192.0.2.20";
        device.peerPort = 5060;
        device.registeredAt = now;
        device.lastRegisterAt = now;
        device.expiresAt = now + 3600;
        device.lastHeartbeatAt = now;
        device.online = online;
        device.sender = [this](const std::string &wire) {
            outbound = wire;
            return sendSucceeds;
        };
        registrations->upsert(device);
    }
};

void testCatalogQueryAndResponse() {
    Fixture fixture;
    fixture.addDevice();
    std::string error;
    const std::string commandId = fixture.platform->queryCatalog(deviceId, &error);
    expect(!commandId.empty() && error.empty(), "online device accepts a Catalog command");
    expect(fixture.platform->pendingTransactions() == 1, "Catalog command creates a transaction");

    SipMessage request;
    size_t consumed = 0;
    expect(SipMessage::parse(fixture.outbound, request, &consumed, &error),
           "outbound Catalog request parses: " + error);
    expect(consumed == fixture.outbound.size(), "outbound Catalog request is fully framed");
    expect(request.isRequest() && request.method() == "MESSAGE",
           "Catalog query is transported as SIP MESSAGE");
    expect(request.requestUri() == "sip:34020000001320000001@3402000000",
           "Catalog request targets the registered device");
    expect(request.header("Via").find("203.0.113.10:5060") != std::string::npos,
           "Catalog Via advertises the configured platform endpoint");
    expect(request.body().find("<CmdType>Catalog</CmdType>") != std::string::npos &&
           request.body().find(std::string("<DeviceID>") + deviceId + "</DeviceID>") != std::string::npos,
           "Catalog request contains the required MANSCDP fields");

    PlatformCommand command;
    fixture.platform->findCommand(commandId, command);
    expect(command.state == PlatformCommandPending, "command remains pending before final response");

    SipMessage trying = SipResponse::fromRequest(request, 100, "Trying", "device-tag");
    SipMessage unused;
    SipPeer peer;
    fixture.processor->process(trying, peer, unused);
    expect(fixture.platform->pendingTransactions() == 1,
           "provisional response does not complete the client transaction");

    SipMessage ok = SipResponse::fromRequest(request, 200, "OK", "device-tag");
    fixture.processor->process(ok, peer, unused);
    fixture.platform->findCommand(commandId, command);
    expect(command.state == PlatformCommandSucceeded && command.sipStatus == 200,
           "matching 200 response completes the Catalog command");
    expect(fixture.platform->pendingTransactions() == 0,
           "completed transaction is removed");
}

void testFailuresAndTimeout() {
    Fixture fixture;
    std::string error;
    expect(fixture.platform->queryCatalog(deviceId, &error).empty() &&
           error.find("not registered") != std::string::npos,
           "unregistered device rejects outbound command");

    fixture.addDevice(false);
    expect(fixture.platform->queryCatalog(deviceId, &error).empty() &&
           error.find("offline") != std::string::npos,
           "offline device rejects outbound command");

    fixture.addDevice(true);
    const std::string timedCommand = fixture.platform->queryCatalog(deviceId, &error);
    fixture.now += fixture.config.transactionTimeoutSeconds;
    fixture.platform->sweep();
    PlatformCommand command;
    fixture.platform->findCommand(timedCommand, command);
    expect(command.state == PlatformCommandTimedOut &&
           command.error.find("timed out") != std::string::npos,
           "expired SIP transaction marks command timed out");

    fixture.sendSucceeds = false;
    const std::string failedCommand = fixture.platform->queryCatalog(deviceId, &error);
    expect(failedCommand.empty() && error.find("failed to send") != std::string::npos,
           "sender failure is returned to caller");
    const std::vector<PlatformCommand> commands = fixture.platform->listCommands();
    bool foundFailed = false;
    for (std::vector<PlatformCommand>::const_iterator it = commands.begin();
         it != commands.end(); ++it) {
        if (it->state == PlatformCommandFailed) {
            foundFailed = true;
        }
    }
    expect(foundFailed, "sender failure is retained in command history");
}

void testNegativeResponse() {
    Fixture fixture;
    fixture.addDevice();
    std::string error;
    const std::string commandId = fixture.platform->queryCatalog(deviceId, &error);
    SipMessage request;
    SipMessage::parse(fixture.outbound, request, nullptr, &error);
    SipMessage forbidden = SipResponse::fromRequest(request, 403, "Forbidden", "device-tag");
    fixture.platform->handleResponse(forbidden);

    PlatformCommand command;
    fixture.platform->findCommand(commandId, command);
    expect(command.state == PlatformCommandFailed && command.sipStatus == 403 &&
           command.error.find("403") != std::string::npos,
           "negative final response marks the command failed with its SIP status");
}

void testMismatchedResponseIgnored() {
    Fixture fixture;
    fixture.addDevice();
    std::string error;
    fixture.platform->queryCatalog(deviceId, &error);
    SipMessage request;
    SipMessage::parse(fixture.outbound, request, nullptr, &error);
    SipMessage response = SipResponse::fromRequest(request, 200, "OK", "device-tag");
    response.setHeader("Call-ID", "different-call-id");
    expect(!fixture.platform->handleResponse(response), "unmatched response is ignored");
    expect(fixture.platform->pendingTransactions() == 1,
           "unmatched response cannot consume another transaction");
}

} // namespace

int main() {
    testCatalogQueryAndResponse();
    testFailuresAndTimeout();
    testNegativeResponse();
    testMismatchedResponseIgnored();

    if (failures != 0) {
        std::cerr << failures << " GB28181 platform-command test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 outbound platform-command tests passed" << std::endl;
    return EXIT_SUCCESS;
}
