#include <cstdlib>
#include <iostream>
#include <string>

#include "core/SipResponse.h"
#include "service/GbSipConfig.h"
#include "service/SipRequestProcessor.h"

using easy_sva::gb28181::BasicSipRequestProcessor;
using easy_sva::gb28181::GbSipConfig;
using easy_sva::gb28181::SipMessage;
using easy_sva::gb28181::SipPeer;
using easy_sva::gb28181::SipResponse;

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

SipMessage makeRequest(const std::string &method) {
    SipMessage request;
    request.setRequestLine(method, "sip:34020000002000000001@3402000000");
    request.addHeader("Via", "SIP/2.0/UDP 192.0.2.15:5060;branch=z9hG4bK-one");
    request.addHeader("Via", "SIP/2.0/UDP 192.0.2.16:5060;branch=z9hG4bK-two");
    request.addHeader("From", "<sip:34020000001320000001@3402000000>;tag=device-tag");
    request.addHeader("To", "<sip:34020000002000000001@3402000000>");
    request.addHeader("Call-ID", "call-1234");
    request.addHeader("CSeq", "1 " + method);
    return request;
}

void testResponseBuilder() {
    const SipMessage request = makeRequest("OPTIONS");
    SipMessage response = SipResponse::fromRequest(request, 200, "OK", "server-tag");
    expect(!response.isRequest() && response.statusCode() == 200, "response status is set");
    expect(response.headerValues("Via").size() == 2, "all Via headers are copied");
    expect(response.header("From") == request.header("From"), "From is copied exactly");
    expect(response.header("To").find(";tag=server-tag") != std::string::npos,
           "final response adds the server To tag");
    expect(response.header("Call-ID") == "call-1234", "Call-ID is copied");
    expect(response.header("CSeq") == "1 OPTIONS", "CSeq is copied");

    SipMessage alreadyTagged = makeRequest("OPTIONS");
    alreadyTagged.setHeader("To", "<sip:platform@example>;tag=existing");
    response = SipResponse::fromRequest(alreadyTagged, 200, "OK", "server-tag");
    expect(response.header("To") == "<sip:platform@example>;tag=existing",
           "an existing To tag is not duplicated");
}

void testDefaultProcessor() {
    BasicSipRequestProcessor processor("server-tag");
    SipPeer peer;
    peer.transport = "UDP";
    peer.ip = "192.0.2.15";
    peer.port = 5060;
    SipMessage response;

    expect(processor.process(makeRequest("OPTIONS"), peer, response), "OPTIONS produces a response");
    expect(response.statusCode() == 200, "OPTIONS returns 200");
    expect(response.header("Allow").find("REGISTER") != std::string::npos,
           "OPTIONS describes the GB28181 methods");

    expect(processor.process(makeRequest("REGISTER"), peer, response), "REGISTER produces a response");
    expect(response.statusCode() == 501, "unimplemented REGISTER is explicit during transport phase");

    expect(processor.process(makeRequest("PUBLISH"), peer, response), "unknown method produces a response");
    expect(response.statusCode() == 405, "unknown method returns 405");
    expect(!processor.process(makeRequest("ACK"), peer, response), "ACK never receives a SIP response");

    SipMessage incomingResponse;
    incomingResponse.setStatusLine(200, "OK");
    expect(!processor.process(incomingResponse, peer, response), "incoming responses are not answered");
}

void testConfigParsing() {
    const std::string valid =
        "[sip]\n"
        "server_id=34020000002000000099\n"
        "realm=3402000000\n"
        "advertised_ip=203.0.113.10\n"
        "transaction_timeout_seconds=8\n"
        "listen_ip=127.0.0.1\n"
        "port=15060\n"
        "udp=yes\n"
        "tcp=off\n"
        "idle_timeout_seconds=90\n"
        "max_message_bytes=65536\n"
        "[registration]\n"
        "auth_required=true\n"
        "device_password=test-secret\n"
        "nonce_ttl_seconds=120\n"
        "default_expires_seconds=600\n"
        "min_expires_seconds=60\n"
        "max_expires_seconds=3600\n"
        "[device]\n"
        "heartbeat_timeout_seconds=75\n";
    GbSipConfig config;
    std::string error;
    expect(GbSipConfig::parse(valid, config, &error), "valid config parses: " + error);
    expect(config.serverId == "34020000002000000099", "server ID is parsed");
    expect(config.advertisedIp == "203.0.113.10" && config.transactionTimeoutSeconds == 8,
           "advertised SIP address and transaction timeout are parsed");
    expect(config.listenIp == "127.0.0.1" && config.sipPort == 15060,
           "listen endpoint is parsed");
    expect(config.enableUdp && !config.enableTcp, "transport switches are parsed");
    expect(config.idleTimeoutSeconds == 90 && config.maxMessageBytes == 65536,
           "resource limits are parsed");
    expect(config.authRequired && config.devicePassword == "test-secret",
           "registration authentication is parsed");
    expect(config.nonceTtlSeconds == 120 && config.defaultRegisterExpires == 600,
           "registration lifetimes are parsed");
    expect(config.heartbeatTimeoutSeconds == 75,
           "heartbeat timeout is parsed");

    expect(!GbSipConfig::parse("[sip]\nserver_id=bad\n", config, &error),
           "non-standard platform IDs are rejected");
    expect(!GbSipConfig::parse("[sip]\nudp=false\ntcp=false\n", config, &error),
           "disabling both transports is rejected");
    expect(!GbSipConfig::parse("[sip]\nport=70000\n", config, &error),
           "out-of-range SIP ports are rejected");
    expect(!GbSipConfig::parse("[sip]\nmax_message_bytes=10\n", config, &error),
           "unsafe message-size limits are rejected");
    expect(!GbSipConfig::parse("[registration]\nauth_required=true\ndevice_password=\n", config, &error),
           "empty password is rejected when Digest authentication is enabled");
    expect(!GbSipConfig::parse(
        "[registration]\nmin_expires_seconds=300\ndefault_expires_seconds=60\n", config, &error),
        "default registration expiry must stay within its configured bounds");
    expect(!GbSipConfig::parse("[device]\nheartbeat_timeout_seconds=1\n", config, &error),
           "unsafe heartbeat timeouts are rejected");
}

} // namespace

int main() {
    testResponseBuilder();
    testDefaultProcessor();
    testConfigParsing();

    if (failures != 0) {
        std::cerr << failures << " GB28181 service-core test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 service-core tests passed" << std::endl;
    return EXIT_SUCCESS;
}
