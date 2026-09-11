#include <cstdlib>
#include <iostream>
#include <string>

#include "core/SipResponse.h"
#include "service/SipRequestFactory.h"

using easy_sva::gb28181::GbSipConfig;
using easy_sva::gb28181::RegisteredDevice;
using easy_sva::gb28181::SipMessage;
using easy_sva::gb28181::SipRequestFactory;
using easy_sva::gb28181::SipResponse;

namespace {

const char *channelId = "34020000001320000002";
int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

GbSipConfig sampleConfig() {
    GbSipConfig config;
    config.serverId = "34020000002000000001";
    config.realm = "3402000000";
    config.advertisedIp = "203.0.113.10";
    config.sipPort = 5060;
    return config;
}

RegisteredDevice sampleDevice() {
    RegisteredDevice device;
    device.deviceId = "34020000001320000001";
    device.transport = "UDP";
    device.peerIp = "192.0.2.20";
    device.peerPort = 5060;
    return device;
}

SipMessage sampleInvite() {
    return SipRequestFactory::liveInvite(
        sampleConfig(), sampleDevice(), channelId,
        "v=0\r\ns=Play\r\nm=video 30000 RTP/AVP 96\r\n",
        "0100000001", 20, "invite-token");
}

SipMessage acceptedResponse(const SipMessage &invite) {
    SipMessage response = SipResponse::fromRequest(invite, 200, "OK", "device-tag");
    response.addHeader("Contact", "<sip:34020000001320000001@192.0.2.20:5060>");
    response.addHeader("Record-Route", "<sip:edge.example.test;lr>");
    response.addHeader("Record-Route", "<sip:core.example.test;lr>");
    return response;
}

void testLiveInvite() {
    const SipMessage invite = sampleInvite();
    expect(invite.isRequest() && invite.method() == "INVITE",
           "live playback starts with SIP INVITE");
    expect(invite.requestUri() == "sip:34020000001320000002@192.0.2.20:5060",
           "INVITE targets the channel at the registered signaling peer");
    expect(invite.header("Via").find("SIP/2.0/UDP 203.0.113.10:5060") == 0,
           "INVITE advertises the configured platform endpoint");
    expect(invite.header("From").find(";tag=invite-token") != std::string::npos &&
           invite.header("To") == "<sip:34020000001320000002@3402000000>",
           "INVITE establishes local and remote dialog identities");
    expect(invite.header("Subject") ==
           "34020000001320000002:0100000001,34020000002000000001:0",
           "INVITE Subject binds channel, SSRC, and platform");
    expect(invite.header("Content-Type") == "application/sdp" &&
           invite.body().find("m=video 30000") != std::string::npos,
           "INVITE carries the SDP media offer");
}

void testAcceptedAck() {
    const SipMessage invite = sampleInvite();
    const SipMessage response = acceptedResponse(invite);
    const SipMessage ack = SipRequestFactory::inviteAck(
        sampleConfig(), invite, response, "ack-token");
    expect(ack.method() == "ACK" &&
           ack.requestUri() == "sip:34020000001320000001@192.0.2.20:5060",
           "2xx ACK uses the response Contact as remote target");
    expect(ack.header("Via").find("branch=z9hG4bK-ack-token") != std::string::npos &&
           ack.header("Via") != invite.header("Via"),
           "2xx ACK starts a separate client transaction branch");
    expect(ack.header("From") == invite.header("From") &&
           ack.header("To").find(";tag=device-tag") != std::string::npos &&
           ack.header("Call-ID") == invite.header("Call-ID") &&
           ack.header("CSeq") == "20 ACK",
           "2xx ACK preserves the established dialog identifiers");
    const std::vector<std::string> routes = ack.headerValues("Route");
    expect(routes.size() == 2 &&
           routes[0].find("core.example.test") != std::string::npos &&
           routes[1].find("edge.example.test") != std::string::npos,
           "UAC route set reverses the response Record-Route order");
}

void testRejectedAck() {
    const SipMessage invite = sampleInvite();
    const SipMessage rejected = SipResponse::fromRequest(
        invite, 486, "Busy Here", "device-tag");
    const SipMessage ack = SipRequestFactory::inviteAck(
        sampleConfig(), invite, rejected, "unused-token");
    expect(ack.method() == "ACK" && ack.requestUri() == invite.requestUri(),
           "non-2xx ACK targets the original INVITE URI");
    expect(ack.header("Via") == invite.header("Via"),
           "non-2xx ACK stays in the INVITE transaction branch");
    expect(ack.header("CSeq") == "20 ACK" &&
           ack.header("To").find(";tag=device-tag") != std::string::npos,
           "non-2xx ACK acknowledges the matching final response");
}

void testDialogBye() {
    const SipMessage invite = sampleInvite();
    const SipMessage response = acceptedResponse(invite);
    const SipMessage bye = SipRequestFactory::dialogBye(
        sampleConfig(), invite, response, 21, "bye-token");
    expect(bye.method() == "BYE" &&
           bye.requestUri() == "sip:34020000001320000001@192.0.2.20:5060",
           "BYE uses the established remote target");
    expect(bye.header("Via").find("branch=z9hG4bK-bye-token") != std::string::npos &&
           bye.header("CSeq") == "21 BYE",
           "BYE uses a fresh branch and a higher dialog CSeq");
    expect(bye.header("From") == invite.header("From") &&
           bye.header("To") == response.header("To") &&
           bye.header("Call-ID") == invite.header("Call-ID"),
           "BYE preserves dialog identities");
    const std::vector<std::string> routes = bye.headerValues("Route");
    expect(routes.size() == 2 &&
           routes[0].find("core.example.test") != std::string::npos,
           "BYE reuses the established UAC route set");
}

} // namespace

int main() {
    testLiveInvite();
    testAcceptedAck();
    testRejectedAck();
    testDialogBye();

    if (failures != 0) {
        std::cerr << failures << " GB28181 SIP-dialog test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 SIP-dialog tests passed" << std::endl;
    return EXIT_SUCCESS;
}
