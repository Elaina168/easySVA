#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/SipResponse.h"
#include "service/GbLiveService.h"

using easy_sva::gb28181::DeviceCatalogStore;
using easy_sva::gb28181::DeviceChannel;
using easy_sva::gb28181::GbLiveService;
using easy_sva::gb28181::GbMediaFailed;
using easy_sva::gb28181::GbMediaInviting;
using easy_sva::gb28181::GbMediaSession;
using easy_sva::gb28181::GbMediaStopped;
using easy_sva::gb28181::GbMediaStopping;
using easy_sva::gb28181::GbMediaStreaming;
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

struct Fixture {
    uint64_t now;
    GbSipConfig config;
    RegistrationStore::Ptr registrations;
    DeviceCatalogStore::Ptr catalogs;
    GbLiveService::Ptr live;
    std::vector<std::string> outbound;
    std::vector<ZlmHttpRequest> zlmRequests;
    bool openSucceeds;
    bool signalingSucceeds;

    Fixture()
        : now(1000), registrations(new RegistrationStore()),
          catalogs(new DeviceCatalogStore()), openSucceeds(true),
          signalingSucceeds(true) {
        config.advertisedIp = "203.0.113.10";
        config.rtpAdvertisedIp = "198.51.100.10";
        config.zlmApiSecret = "test-secret";
        config.transactionTimeoutSeconds = 5;
        rebuild();
    }

    void rebuild() {
        GbLiveService::Clock clock = [this]() { return now; };
        live.reset(new GbLiveService(
            config, registrations, catalogs, clock,
            [this](const ZlmHttpRequest &request,
                   const ZlmApiClient::HttpCompletion &completion) {
                zlmRequests.push_back(request);
                ZlmHttpResponse response;
                response.statusCode = 200;
                if (request.url.find("openRtpServer") != std::string::npos) {
                    response.body = openSucceeds
                        ? "{\"code\":0,\"port\":30000}"
                        : "{\"code\":-1,\"msg\":\"bind failed\"}";
                } else {
                    response.body = "{\"code\":0,\"hit\":1}";
                }
                completion(response);
            }));
    }

    void addDevice(bool online = true) {
        RegisteredDevice device;
        device.deviceId = deviceId;
        device.transport = "UDP";
        device.peerIp = "192.0.2.20";
        device.peerPort = 5060;
        device.online = online;
        device.registeredAt = now;
        device.lastRegisterAt = now;
        device.lastHeartbeatAt = now;
        device.expiresAt = now + 3600;
        device.sender = [this](const std::string &wire) {
            if (signalingSucceeds) {
                outbound.push_back(wire);
                return true;
            }
            return false;
        };
        registrations->upsert(device);
    }

    void addChannel(const std::string &status = "ON") {
        DeviceChannel channel;
        channel.deviceId = channelId;
        channel.status = status;
        std::vector<DeviceChannel> channels(1, channel);
        catalogs->replace(deviceId, channels, now);
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

    size_t zlmCallCount(const std::string &endpoint) const {
        size_t count = 0;
        for (std::vector<ZlmHttpRequest>::const_iterator it = zlmRequests.begin();
             it != zlmRequests.end(); ++it) {
            if (it->url.find(endpoint) != std::string::npos) {
                ++count;
            }
        }
        return count;
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
                         int status,
                         const std::string &reason,
                         const std::string &body = std::string()) {
    SipMessage response = SipResponse::fromRequest(
        request, status, reason, "device-tag", body,
        body.empty() ? std::string() : "application/sdp");
    if (status >= 200 && status < 300 && request.method() == "INVITE") {
        response.addHeader("Contact",
            "<sip:34020000001320000001@192.0.2.20:5060>");
    }
    return response;
}

void testSuccessfulLifecycle() {
    Fixture fixture;
    fixture.addDevice();
    fixture.addChannel();
    std::string error;
    const std::string sessionId = fixture.live->startLive(deviceId, channelId, &error);
    expect(!sessionId.empty() && error.empty(),
           "registered online channel starts a live session: " + error);
    expect(fixture.zlmCallCount("openRtpServer") == 1 &&
           fixture.outbound.size() == 1,
           "start allocates an RTP receiver before sending INVITE");

    const SipMessage invite = fixture.lastRequest("INVITE");
    expect(invite.method() == "INVITE" &&
           invite.body().find("c=IN IP4 198.51.100.10") != std::string::npos &&
           invite.body().find("m=video 30000 RTP/AVP 96") != std::string::npos,
           "INVITE advertises the configured RTP address and allocated port");
    GbMediaSession session;
    fixture.live->findSession(sessionId, session);
    expect(session.state == GbMediaInviting && session.rtpPort == 30000,
           "session waits in inviting state with its allocated RTP port");

    const SipMessage accepted = finalResponse(
        invite, 200, "OK", validAnswer(session.ssrc));
    expect(fixture.live->handleResponse(accepted),
           "matching INVITE response is consumed");
    fixture.live->findSession(sessionId, session);
    expect(session.state == GbMediaStreaming &&
           fixture.lastRequest("ACK").method() == "ACK",
           "valid 200/SDP is ACKed and moves the session to streaming");

    const size_t beforeRetransmit = fixture.outbound.size();
    expect(fixture.live->handleResponse(accepted) &&
           fixture.outbound.size() == beforeRetransmit + 1 &&
           fixture.lastRequest("ACK").method() == "ACK",
           "retransmitted 200 response is ACKed again without reopening media");

    expect(fixture.live->stopLive(sessionId, &error),
           "streaming session accepts an explicit stop: " + error);
    const SipMessage bye = fixture.lastRequest("BYE");
    fixture.live->findSession(sessionId, session);
    expect(bye.method() == "BYE" && session.state == GbMediaStopping,
           "stop sends BYE and enters stopping state");
    expect(fixture.live->handleResponse(finalResponse(bye, 200, "OK")),
           "matching BYE response is consumed");
    fixture.live->findSession(sessionId, session);
    expect(session.state == GbMediaStopped &&
           fixture.zlmCallCount("closeRtpServer") == 1,
           "successful BYE closes the ZLM RTP receiver and stops the session");
}

void testInviteRejected() {
    Fixture fixture;
    fixture.addDevice();
    fixture.addChannel();
    std::string error;
    const std::string sessionId = fixture.live->startLive(deviceId, channelId, &error);
    const SipMessage invite = fixture.lastRequest("INVITE");
    fixture.live->handleResponse(finalResponse(invite, 486, "Busy Here"));
    GbMediaSession session;
    fixture.live->findSession(sessionId, session);
    expect(session.state == GbMediaFailed && session.error.find("486") != std::string::npos,
           "negative INVITE response fails the media session");
    expect(fixture.lastRequest("ACK").method() == "ACK" &&
           fixture.zlmCallCount("closeRtpServer") == 1,
           "negative INVITE response is ACKed and releases the RTP receiver");
}

void testRtpAllocationFailure() {
    Fixture fixture;
    fixture.openSucceeds = false;
    fixture.addDevice();
    fixture.addChannel();
    std::string error;
    const std::string sessionId = fixture.live->startLive(deviceId, channelId, &error);
    GbMediaSession session;
    fixture.live->findSession(sessionId, session);
    expect(session.state == GbMediaFailed &&
           session.error.find("bind failed") != std::string::npos,
           "ZLM allocation error is retained on the session");
    expect(fixture.outbound.empty(),
           "INVITE is never sent when RTP allocation fails");
}

void testSignalingSendFailure() {
    Fixture fixture;
    fixture.signalingSucceeds = false;
    fixture.addDevice();
    fixture.addChannel();
    std::string error;
    const std::string sessionId = fixture.live->startLive(deviceId, channelId, &error);
    GbMediaSession session;
    fixture.live->findSession(sessionId, session);
    expect(session.state == GbMediaFailed &&
           session.error.find("failed to send SIP request") != std::string::npos,
           "synchronous INVITE send failure is retained on the session");
    expect(fixture.zlmCallCount("closeRtpServer") == 1,
           "synchronous INVITE send failure releases the RTP receiver");
}

void testInvalidAnswerIsTerminated() {
    Fixture fixture;
    fixture.addDevice();
    fixture.addChannel();
    std::string error;
    const std::string sessionId = fixture.live->startLive(deviceId, channelId, &error);
    GbMediaSession session;
    fixture.live->findSession(sessionId, session);
    const std::string invalidAnswer =
        "v=0\r\ns=Play\r\nc=IN IP4 192.0.2.20\r\nt=0 0\r\n"
        "m=video 62000 RTP/AVP 98\r\na=sendonly\r\n"
        "a=rtpmap:98 H264/90000\r\ny=" + session.ssrc + "\r\n";
    fixture.live->handleResponse(finalResponse(
        fixture.lastRequest("INVITE"), 200, "OK", invalidAnswer));
    fixture.live->findSession(sessionId, session);
    expect(session.state == GbMediaStopping &&
           fixture.lastRequest("ACK").method() == "ACK" &&
           fixture.lastRequest("BYE").method() == "BYE",
           "accepted but incompatible SDP is ACKed then terminated with BYE");
    fixture.live->handleResponse(finalResponse(
        fixture.lastRequest("BYE"), 200, "OK"));
    fixture.live->findSession(sessionId, session);
    expect(session.state == GbMediaFailed &&
           session.error.find("PS/90000") != std::string::npos &&
           fixture.zlmCallCount("closeRtpServer") == 1,
           "invalid SDP termination closes RTP and preserves the root cause");
}

void testTimeoutAndInputValidation() {
    Fixture fixture;
    fixture.addDevice();
    fixture.addChannel();
    std::string error;
    const std::string sessionId = fixture.live->startLive(deviceId, channelId, &error);
    expect(fixture.live->startLive(deviceId, channelId, &error).empty() &&
           error.find("active media session") != std::string::npos,
           "a channel cannot start a second active session");
    fixture.now += fixture.config.transactionTimeoutSeconds;
    fixture.live->sweep();
    GbMediaSession session;
    fixture.live->findSession(sessionId, session);
    expect(session.state == GbMediaFailed &&
           session.error.find("timed out") != std::string::npos &&
           fixture.zlmCallCount("closeRtpServer") == 1,
           "INVITE timeout fails the session and releases RTP");

    Fixture activeTcp;
    activeTcp.config.rtpTcpMode = 2;
    activeTcp.rebuild();
    activeTcp.addDevice();
    activeTcp.addChannel();
    expect(activeTcp.live->startLive(deviceId, channelId, &error).empty() &&
           error.find("connectRtpServer") != std::string::npos,
           "TCP active mode is rejected until its required connect step exists");
}

} // namespace

int main() {
    testSuccessfulLifecycle();
    testInviteRejected();
    testRtpAllocationFailure();
    testSignalingSendFailure();
    testInvalidAnswerIsTerminated();
    testTimeoutAndInputValidation();

    if (failures != 0) {
        std::cerr << failures << " GB28181 live-service test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 live-service tests passed" << std::endl;
    return EXIT_SUCCESS;
}
