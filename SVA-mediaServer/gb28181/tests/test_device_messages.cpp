#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "service/DeviceCatalogStore.h"
#include "service/DigestNonceStore.h"
#include "service/GbSipRequestProcessor.h"
#include "service/RegistrationStore.h"

using easy_sva::gb28181::DeviceCatalogStore;
using easy_sva::gb28181::DeviceChannel;
using easy_sva::gb28181::DigestNonceStore;
using easy_sva::gb28181::GbSipConfig;
using easy_sva::gb28181::GbSipRequestProcessor;
using easy_sva::gb28181::RegisteredDevice;
using easy_sva::gb28181::RegistrationStore;
using easy_sva::gb28181::SipMessage;
using easy_sva::gb28181::SipPeer;

namespace {

const char *deviceId = "34020000001320000001";
int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

SipMessage request(const std::string &method) {
    SipMessage message;
    message.setRequestLine(method, "sip:34020000002000000001@3402000000");
    message.addHeader("Via", "SIP/2.0/UDP 192.0.2.15:5060;branch=z9hG4bK-message");
    message.addHeader("From", std::string("<sip:") + deviceId + "@3402000000>;tag=device");
    message.addHeader("To", "<sip:34020000002000000001@3402000000>");
    message.addHeader("Call-ID", "device-message-call");
    message.addHeader("CSeq", std::string("1 ") + method);
    return message;
}

SipMessage registration() {
    SipMessage message = request("REGISTER");
    message.addHeader("Contact", std::string("<sip:") + deviceId + "@192.0.2.15:5060>");
    message.addHeader("Expires", "3600");
    return message;
}

SipMessage xmlMessage(const std::string &xml) {
    SipMessage message = request("MESSAGE");
    message.addHeader("Content-Type", "Application/MANSCDP+xml; charset=UTF-8");
    message.setBody(xml);
    return message;
}

std::string keepalive(const std::string &status = "OK") {
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") +
        "<Notify><CmdType>Keepalive</CmdType><SN>1</SN><DeviceID>" + deviceId +
        "</DeviceID><Status>" + status + "</Status></Notify>";
}

std::string catalog(const std::string &items, size_t sumNum) {
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") +
        "<Response><CmdType>Catalog</CmdType><SN>2</SN><DeviceID>" + deviceId +
        "</DeviceID><SumNum>" + std::to_string(sumNum) +
        "</SumNum><DeviceList Num=\"" + std::to_string(sumNum) + "\">" + items +
        "</DeviceList></Response>";
}

std::string item(const std::string &channelId,
                 const std::string &name,
                 const std::string &status = "ON") {
    return "<Item><DeviceID>" + channelId + "</DeviceID><Name>" + name +
        "</Name><Manufacturer>easySVA</Manufacturer><Model>IPC-Test</Model>"
        "<ParentID>" + deviceId + "</ParentID><Parental>0</Parental>"
        "<Status>" + status + "</Status><Longitude>116.3</Longitude>"
        "<Latitude>39.9</Latitude></Item>";
}

struct Fixture {
    uint64_t now;
    GbSipConfig config;
    RegistrationStore::Ptr registrations;
    DigestNonceStore::Ptr nonces;
    DeviceCatalogStore::Ptr catalogs;
    std::shared_ptr<GbSipRequestProcessor> processor;
    SipPeer peer;

    Fixture()
        : now(1000), registrations(new RegistrationStore()),
          nonces(new DigestNonceStore(300)), catalogs(new DeviceCatalogStore()) {
        config.authRequired = false;
        config.heartbeatTimeoutSeconds = 10;
        GbSipRequestProcessor::Clock clock = [this]() { return now; };
        processor.reset(new GbSipRequestProcessor(
            config, registrations, nonces, catalogs, clock));
        peer.transport = "UDP";
        peer.ip = "192.0.2.15";
        peer.port = 5060;
    }

    void registerDevice() {
        SipMessage response;
        processor->process(registration(), peer, response);
        expect(response.statusCode() == 200, "fixture device registers without Digest");
    }
};

void testKeepaliveAndTimeout() {
    Fixture fixture;
    SipMessage response;
    fixture.processor->process(xmlMessage(keepalive()), fixture.peer, response);
    expect(response.statusCode() == 403, "an unregistered device cannot send Keepalive");

    fixture.registerDevice();
    fixture.now += 5;
    fixture.peer.ip = "198.51.100.22";
    fixture.peer.port = 25060;
    fixture.processor->process(xmlMessage(keepalive()), fixture.peer, response);
    expect(response.statusCode() == 200, "registered device Keepalive returns 200");

    RegisteredDevice device;
    fixture.registrations->find(deviceId, device);
    expect(device.online && device.lastHeartbeatAt == 1005,
           "Keepalive refreshes online state and timestamp");
    expect(device.peerIp == "198.51.100.22" && device.peerPort == 25060,
           "Keepalive refreshes the current NAT signaling endpoint");

    fixture.now += 10;
    fixture.processor->sweep();
    fixture.registrations->find(deviceId, device);
    expect(!device.online, "device is marked offline at the heartbeat timeout");
    expect(fixture.registrations->size() == 1,
           "heartbeat timeout retains registration metadata until Expires");

    fixture.processor->process(xmlMessage(keepalive()), fixture.peer, response);
    fixture.registrations->find(deviceId, device);
    expect(device.online, "a later valid Keepalive brings the device online again");

    fixture.now += 1;
    fixture.processor->process(xmlMessage(keepalive("ERROR")), fixture.peer, response);
    expect(response.statusCode() == 400, "non-OK Keepalive status is rejected");
    fixture.registrations->find(deviceId, device);
    expect(device.lastHeartbeatAt == 1015, "rejected Keepalive does not refresh liveness");
}

void testCatalogSnapshots() {
    Fixture fixture;
    fixture.registerDevice();
    SipMessage response;
    const std::string first =
        item("34020000001320000002", "Entrance") +
        item("34020000001320000003", "Warehouse", "OFF");
    fixture.processor->process(xmlMessage(catalog(first, 2)), fixture.peer, response);
    expect(response.statusCode() == 200, "complete Catalog response returns 200");
    expect(fixture.catalogs->size(deviceId) == 2, "complete Catalog snapshot stores all channels");

    DeviceChannel channel;
    expect(fixture.catalogs->find(deviceId, "34020000001320000003", channel),
           "Catalog channel is addressable by parent device and channel ID");
    expect(channel.name == "Warehouse" && channel.status == "OFF" &&
           channel.lastCatalogAt == 1000,
           "Catalog metadata and observation time are retained");

    fixture.now += 1;
    fixture.processor->process(xmlMessage(catalog(
        item("34020000001320000002", "Entrance Updated"), 1)), fixture.peer, response);
    expect(fixture.catalogs->size(deviceId) == 1,
           "a complete later snapshot removes channels no longer reported");
    fixture.catalogs->find(deviceId, "34020000001320000002", channel);
    expect(channel.name == "Entrance Updated", "later snapshot replaces channel metadata");

    fixture.processor->process(xmlMessage(catalog(
        item("34020000001320000004", "Partial Page"), 2)), fixture.peer, response);
    expect(fixture.catalogs->size(deviceId) == 2,
           "a partial Catalog page is merged without deleting the prior snapshot");
}

void testXmlValidationAndGb2312() {
    Fixture fixture;
    fixture.registerDevice();
    SipMessage response;
    fixture.processor->process(xmlMessage("<Notify><CmdType>Keepalive"), fixture.peer, response);
    expect(response.statusCode() == 400, "malformed MANSCDP XML returns 400");

    SipMessage unsupported = xmlMessage(keepalive());
    unsupported.setHeader("Content-Type", "application/json");
    fixture.processor->process(unsupported, fixture.peer, response);
    expect(response.statusCode() == 415, "non-MANSCDP MESSAGE bodies return 415");

    const std::string gbName("\xC9\xE3\xCF\xF1\xBB\xFA", 6);
    const std::string gbXml =
        std::string("<?xml version=\"1.0\" encoding=\"GB2312\"?><Response>") +
        "<CmdType>Catalog</CmdType><SN>9</SN><DeviceID>" + deviceId +
        "</DeviceID><SumNum>1</SumNum><DeviceList Num=\"1\"><Item><DeviceID>"
        "34020000001320000009</DeviceID><Name>" + gbName +
        "</Name><Status>ON</Status></Item></DeviceList></Response>";
    fixture.processor->process(xmlMessage(gbXml), fixture.peer, response);
    expect(response.statusCode() == 200, "GB2312 Catalog XML is converted and accepted");
    DeviceChannel channel;
    fixture.catalogs->find(deviceId, "34020000001320000009", channel);
    expect(channel.name == u8"摄像机", "GB2312 channel names are stored as UTF-8");
}

} // namespace

int main() {
    testKeepaliveAndTimeout();
    testCatalogSnapshots();
    testXmlValidationAndGb2312();

    if (failures != 0) {
        std::cerr << failures << " GB28181 device-message test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 Keepalive/Catalog tests passed" << std::endl;
    return EXIT_SUCCESS;
}
