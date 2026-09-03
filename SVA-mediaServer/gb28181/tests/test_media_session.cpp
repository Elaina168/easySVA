#include <cstdlib>
#include <iostream>
#include <string>

#include "service/GbSdp.h"
#include "service/MediaSessionStore.h"

using easy_sva::gb28181::GbMediaFailed;
using easy_sva::gb28181::GbMediaInviting;
using easy_sva::gb28181::GbMediaPreparing;
using easy_sva::gb28181::GbMediaSession;
using easy_sva::gb28181::GbMediaStopped;
using easy_sva::gb28181::GbMediaStopping;
using easy_sva::gb28181::GbMediaStreaming;
using easy_sva::gb28181::GbSdp;
using easy_sva::gb28181::GbSdpDescription;
using easy_sva::gb28181::GbSdpOffer;
using easy_sva::gb28181::MediaSessionStore;

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

GbSdpOffer sampleOffer(bool tcp) {
    GbSdpOffer offer;
    offer.platformId = "34020000002000000001";
    offer.channelId = "34020000001320000001";
    offer.destinationIp = "192.0.2.10";
    offer.destinationPort = 30000;
    offer.ssrc = "0100000001";
    offer.tcpPassive = tcp;
    return offer;
}

GbMediaSession sampleSession(const std::string &sessionId,
                             const std::string &callId) {
    GbMediaSession session;
    session.sessionId = sessionId;
    session.deviceId = "34020000001320000001";
    session.channelId = "34020000001320000002";
    session.streamId = "gb_34020000001320000002_0100000001";
    session.callId = callId;
    session.ssrc = "0100000001";
    session.rtpPort = 30000;
    session.createdAt = 1000;
    session.updatedAt = 1000;
    return session;
}

void testUdpPlayOffer() {
    std::string sdp;
    std::string error;
    expect(GbSdp::buildPlayOffer(sampleOffer(false), sdp, &error),
           "UDP real-time play offer builds: " + error);
    expect(sdp.find("s=Play\r\n") != std::string::npos &&
           sdp.find("u=34020000001320000001:0\r\n") != std::string::npos,
           "play offer identifies the live channel");
    expect(sdp.find("m=video 30000 RTP/AVP 96\r\n") != std::string::npos,
           "UDP offer announces the RTP receiver port");
    expect(sdp.find("a=recvonly\r\n") != std::string::npos &&
           sdp.find("a=rtpmap:96 PS/90000\r\n") != std::string::npos,
           "offer asks the device to send a PS stream");
    expect(sdp.find("y=0100000001\r\n") != std::string::npos,
           "offer carries the decimal SSRC");

    GbSdpDescription parsed;
    expect(GbSdp::parse(sdp, parsed, &error), "generated UDP offer parses: " + error);
    expect(!parsed.isTcp() && parsed.mediaPort == 30000,
           "parsed UDP offer preserves transport and port");
    expect(parsed.supportsPs() && parsed.direction == "recvonly",
           "parsed UDP offer preserves PS mapping and direction");
}

void testTcpPassivePlayOffer() {
    std::string sdp;
    std::string error;
    expect(GbSdp::buildPlayOffer(sampleOffer(true), sdp, &error),
           "TCP passive play offer builds: " + error);
    expect(sdp.find("TCP/RTP/AVP") != std::string::npos &&
           sdp.find("a=setup:passive\r\n") != std::string::npos &&
           sdp.find("a=connection:new\r\n") != std::string::npos,
           "TCP offer declares RFC 4571 passive setup");
    GbSdpDescription parsed;
    expect(GbSdp::parse(sdp, parsed, &error) && parsed.isTcp() &&
           parsed.setup == "passive" && parsed.connection == "new",
           "TCP setup attributes survive parsing");
}

void testDeviceAnswerParsing() {
    const std::string answer =
        "v=0\r\n"
        "o=34020000001320000001 0 0 IN IP4 198.51.100.20\r\n"
        "s=Play\r\n"
        "c=IN IP4 198.51.100.20\r\n"
        "t=0 0\r\n"
        "m=video 62000 RTP/AVP 96 98\r\n"
        "a=sendonly\r\n"
        "a=rtpmap:96 PS/90000\r\n"
        "a=rtpmap:98 H264/90000\r\n"
        "y=0100000001\r\n";
    GbSdpDescription parsed;
    std::string error;
    expect(GbSdp::parse(answer, parsed, &error), "device SDP answer parses: " + error);
    expect(parsed.connectionAddress == "198.51.100.20" && parsed.mediaPort == 62000,
           "device answer exposes its media endpoint");
    expect(parsed.direction == "sendonly" && parsed.supportsPs(),
           "device answer confirms send direction and PS payload");
    expect(parsed.payloadTypes.size() == 2 && parsed.ssrc == "0100000001",
           "device answer keeps payload choices and SSRC");
}

void testFirstVideoSectionWins() {
    const std::string answer =
        "v=0\r\ns=Play\r\nc=IN IP4 198.51.100.20\r\nt=0 0\r\n"
        "m=video 62000 RTP/AVP 96\r\n"
        "a=sendonly\r\na=rtpmap:96 PS/90000\r\n"
        "m=video 63000 TCP/RTP/AVP 98\r\n"
        "a=recvonly\r\na=setup:active\r\na=rtpmap:98 H264/90000\r\n"
        "y=0100000001\r\n";
    GbSdpDescription parsed;
    std::string error;
    expect(GbSdp::parse(answer, parsed, &error),
           "multi-video SDP parses deterministically: " + error);
    expect(parsed.mediaPort == 62000 && !parsed.isTcp(),
           "parser keeps the first video media endpoint");
    expect(parsed.direction == "sendonly" && parsed.supportsPs() &&
           parsed.rtpMaps.find(98) == parsed.rtpMaps.end(),
           "later video attributes cannot overwrite the selected media section");
}

void testSdpValidation() {
    std::string sdp;
    std::string error;
    GbSdpOffer invalid = sampleOffer(false);
    invalid.ssrc = "999";
    expect(!GbSdp::buildPlayOffer(invalid, sdp, &error) &&
           error.find("10 decimal digits") != std::string::npos,
           "offer rejects malformed SSRC");

    const std::string noConnection =
        "v=0\r\ns=Play\r\nt=0 0\r\n"
        "m=video 30000 RTP/AVP 96\r\n"
        "a=rtpmap:96 PS/90000\r\ny=0100000001\r\n";
    GbSdpDescription parsed;
    expect(!GbSdp::parse(noConnection, parsed, &error) &&
           error.find("connection address") != std::string::npos,
           "parser rejects SDP without a media destination");

    uint32_t numericSsrc = 0;
    expect(GbSdp::parseSsrc("0100000001", numericSsrc, &error) &&
           numericSsrc == 100000001,
           "decimal SSRC converts to the value expected by ZLMediaKit");
    expect(!GbSdp::parseSsrc("9999999999", numericSsrc, &error),
           "SSRC above the 32-bit RTP range is rejected");
}

void testMediaSessionLifecycle() {
    MediaSessionStore store;
    std::string error;
    GbMediaSession session = sampleSession("session-1", "call-1");
    expect(store.create(session, &error), "media session is created: " + error);
    expect(store.size() == 1, "created media session is retained");

    GbMediaSession duplicate = sampleSession("session-2", "call-2");
    expect(!store.create(duplicate, &error) && error.find("active") != std::string::npos,
           "one channel cannot start a duplicate active stream");
    expect(store.transition("session-1", GbMediaPreparing, GbMediaInviting, 1001,
                            std::string(), &error),
           "prepared session transitions to inviting: " + error);
    expect(store.transition("session-1", GbMediaInviting, GbMediaStreaming, 1002,
                            std::string(), &error),
           "accepted INVITE transitions to streaming: " + error);
    expect(!store.transition("session-1", GbMediaInviting, GbMediaFailed, 1003,
                             "late failure", &error) && error.find("streaming") != std::string::npos,
           "stale transition cannot overwrite a newer state");
    expect(store.transition("session-1", GbMediaStreaming, GbMediaStopping, 1004,
                            std::string(), &error) &&
           store.transition("session-1", GbMediaStopping, GbMediaStopped, 1005,
                            std::string(), &error),
           "streaming session follows the stop lifecycle");

    GbMediaSession found;
    expect(store.findByCallId("call-1", found) && found.state == GbMediaStopped,
           "SIP Call-ID resolves the corresponding stopped media session");
    expect(store.eraseTerminal("session-1", &error) && store.size() == 0,
           "terminal session can be removed with its Call-ID index");
    expect(store.create(duplicate, &error),
           "channel can be started again after terminal session cleanup");
}

void testFailureLifecycle() {
    MediaSessionStore store;
    std::string error;
    GbMediaSession session = sampleSession("session-failed", "call-failed");
    expect(store.create(session, &error) &&
           store.transition(session.sessionId, GbMediaPreparing, GbMediaFailed,
                            1001, "RTP receiver allocation failed", &error),
           "preparation failure reaches terminal failed state");
    GbMediaSession found;
    expect(store.find(session.sessionId, found) && found.state == GbMediaFailed &&
           found.error == "RTP receiver allocation failed",
           "failed session retains its diagnostic reason");
    expect(store.eraseTerminal(session.sessionId, &error),
           "failed session can be cleaned up");
}

} // namespace

int main() {
    testUdpPlayOffer();
    testTcpPassivePlayOffer();
    testDeviceAnswerParsing();
    testFirstVideoSectionWins();
    testSdpValidation();
    testMediaSessionLifecycle();
    testFailureLifecycle();

    if (failures != 0) {
        std::cerr << failures << " GB28181 media-session test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 SDP/media-session tests passed" << std::endl;
    return EXIT_SUCCESS;
}
