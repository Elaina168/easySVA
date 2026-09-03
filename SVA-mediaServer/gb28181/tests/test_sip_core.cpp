#include <cstdlib>
#include <iostream>
#include <string>

#include "core/DigestAuth.h"
#include "core/SipMessage.h"
#include "core/SipStreamDecoder.h"

using easy_sva::gb28181::DigestAuth;
using easy_sva::gb28181::DigestCredentials;
using easy_sva::gb28181::SipMessage;
using easy_sva::gb28181::SipStreamDecoder;

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

void testRegisterParsing() {
    const std::string body =
        "<?xml version=\"1.0\"?>\r\n"
        "<Notify><CmdType>Keepalive</CmdType></Notify>";
    const std::string wire =
        "MESSAGE sip:34020000002000000001@3402000000 SIP/2.0\r\n"
        "v: SIP/2.0/UDP 192.0.2.15:5060;branch=z9hG4bK-one\r\n"
        "Via: SIP/2.0/UDP 192.0.2.15:5060;branch=z9hG4bK-two\r\n"
        "From: <sip:34020000001320000001@3402000000>;tag=from-tag\r\n"
        "To: <sip:34020000002000000001@3402000000>\r\n"
        "i: call-1234\r\n"
        "CSeq: 20 MESSAGE\r\n"
        "c: Application/MANSCDP+xml\r\n"
        "l: " + std::to_string(body.size()) + "\r\n\r\n" + body + "NEXT";

    SipMessage message;
    size_t consumed = 0;
    std::string error;
    expect(SipMessage::parse(wire, message, &consumed, &error), "MESSAGE should parse: " + error);
    expect(message.isRequest(), "MESSAGE is a request");
    expect(message.method() == "MESSAGE", "request method is MESSAGE");
    expect(message.requestUri() == "sip:34020000002000000001@3402000000", "request URI is preserved");
    expect(message.headerValues("Via").size() == 2, "compact and long Via names are equivalent");
    expect(message.header("Call-ID") == "call-1234", "compact Call-ID is resolved");
    expect(message.header("Content-Type") == "Application/MANSCDP+xml", "compact Content-Type is resolved");
    expect(message.body() == body, "body follows Content-Length rather than datagram tail");
    expect(consumed == wire.size() - 4, "parser reports one complete SIP frame");
}

void testResponseAndRoundTrip() {
    SipMessage response;
    response.setStatusLine(401, "Unauthorized");
    response.addHeader("Via", "SIP/2.0/UDP 192.0.2.15:5060;branch=z9hG4bK-one");
    response.addHeader("Call-ID", "call-1234");
    response.setHeader("Content-Type", "application/sdp");
    response.setBody("v=0\r\n");

    const std::string wire = response.serialize();
    SipMessage parsed;
    std::string error;
    expect(SipMessage::parse(wire, parsed, nullptr, &error), "serialized response should parse: " + error);
    expect(!parsed.isRequest(), "response is not a request");
    expect(parsed.statusCode() == 401, "response status code is preserved");
    expect(parsed.reasonPhrase() == "Unauthorized", "response reason is preserved");
    expect(parsed.body() == "v=0\r\n", "serialized body is preserved");
    expect(parsed.header("Content-Length") == "5", "serializer writes the exact body length");
}

void testMalformedMessages() {
    SipMessage message;
    std::string error;
    expect(!SipMessage::parse("REGISTER sip:x SIP/2.0\r\nVia: x\r\n", message, nullptr, &error),
           "unterminated headers are rejected");
    expect(!SipMessage::parse("REGISTER sip:x SIP/3.0\r\nContent-Length: 0\r\n\r\n", message, nullptr, &error),
           "unsupported SIP versions are rejected");
    expect(!SipMessage::parse("REGISTER sip:x SIP/2.0\r\nBroken\r\n\r\n", message, nullptr, &error),
           "headers without a colon are rejected");
    expect(!SipMessage::parse("REGISTER sip:x SIP/2.0\r\nContent-Length: -1\r\n\r\n", message, nullptr, &error),
           "negative Content-Length is rejected");
    expect(!SipMessage::parse("REGISTER sip:x SIP/2.0\r\nContent-Length: 3\r\n\r\nx", message, nullptr, &error),
           "incomplete bodies are rejected");
    expect(!SipMessage::parse(
        "REGISTER sip:x SIP/2.0\r\nContent-Length: 0\r\nl: 1\r\n\r\nx", message, nullptr, &error),
        "conflicting compact Content-Length values are rejected");
}

DigestCredentials rfcDigestCredentials() {
    DigestCredentials credentials;
    credentials.username = "Mufasa";
    credentials.realm = "testrealm@host.com";
    credentials.nonce = "dcd98b7102dd2f0e8b11d0f600bfb0c093";
    credentials.uri = "/dir/index.html";
    credentials.algorithm = "MD5";
    credentials.qop = "auth";
    credentials.nonceCount = "00000001";
    credentials.clientNonce = "0a4f113b";
    return credentials;
}

void testDigestReferenceVector() {
    DigestCredentials credentials = rfcDigestCredentials();
    expect(DigestAuth::computeResponse("GET", "Circle Of Life", credentials) ==
               "6629fae49393a05397450978507c4ef1",
           "Digest MD5 qop=auth matches the RFC reference vector");

    const std::string header =
        "Digest username=\"Mufasa\", realm=\"testrealm@host.com\", "
        "nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", uri=\"/dir/index.html\", "
        "qop=auth, nc=00000001, cnonce=\"0a4f113b\", "
        "response=\"6629fae49393a05397450978507c4ef1\", algorithm=MD5";
    DigestCredentials parsed;
    std::string error;
    expect(DigestAuth::parseCredentials(header, parsed, &error), "Digest header should parse: " + error);
    expect(parsed.username == "Mufasa" && parsed.qop == "auth", "Digest fields are extracted");
}

void testSipDigestVerification() {
    DigestCredentials credentials;
    credentials.username = "34020000001320000001";
    credentials.realm = "3402000000";
    credentials.nonce = "server-nonce";
    credentials.uri = "sip:34020000002000000001@3402000000";
    const std::string response = DigestAuth::computeResponse("REGISTER", "device-secret", credentials);

    SipMessage request;
    request.setRequestLine("REGISTER", credentials.uri);
    request.addHeader("Authorization",
        "Digest username=\"" + credentials.username + "\", realm=\"" + credentials.realm +
        "\", nonce=\"" + credentials.nonce + "\", uri=\"" + credentials.uri +
        "\", response=\"" + response + "\", algorithm=MD5");

    std::string error;
    expect(DigestAuth::verifyRequest(request, "device-secret", credentials.realm,
                                     credentials.nonce, &error),
           "valid SIP REGISTER credentials pass verification: " + error);
    expect(!DigestAuth::verifyRequest(request, "wrong-secret", credentials.realm,
                                      credentials.nonce, &error),
           "wrong device password fails verification");
    expect(!DigestAuth::verifyRequest(request, "device-secret", credentials.realm,
                                      "expired-nonce", &error),
           "a nonce from another challenge fails verification");

    const std::string challenge = DigestAuth::buildChallenge("3402000000", "nonce-value", true);
    expect(challenge.find("realm=\"3402000000\"") != std::string::npos,
           "challenge contains the realm");
    expect(challenge.find("qop=\"auth\"") != std::string::npos,
           "challenge advertises qop=auth");
    expect(challenge.find("stale=true") != std::string::npos,
           "stale challenges are marked");
}

void testTcpStreamFraming() {
    SipMessage first;
    first.setRequestLine("OPTIONS", "sip:platform@example");
    first.addHeader("Call-ID", "first");
    SipMessage second;
    second.setRequestLine("REGISTER", "sip:platform@example");
    second.addHeader("Call-ID", "second");
    const std::string firstWire = first.serialize();
    const std::string secondWire = second.serialize();

    SipStreamDecoder decoder(4096);
    std::string error;
    expect(decoder.append(firstWire.substr(0, 11), &error), "first TCP fragment is accepted");
    SipMessage decoded;
    expect(decoder.next(decoded, &error) == SipStreamDecoder::NeedMoreData,
           "partial TCP request waits for more data");
    expect(decoder.append(firstWire.substr(11) + secondWire, &error),
           "coalesced TCP remainder is accepted");
    expect(decoder.next(decoded, &error) == SipStreamDecoder::MessageReady,
           "first coalesced request is decoded");
    expect(decoded.method() == "OPTIONS" && decoded.header("Call-ID") == "first",
           "first TCP request stays intact");
    expect(decoder.next(decoded, &error) == SipStreamDecoder::MessageReady,
           "second coalesced request is decoded");
    expect(decoded.method() == "REGISTER" && decoded.header("Call-ID") == "second",
           "second TCP request stays intact");
    expect(decoder.next(decoded, &error) == SipStreamDecoder::NeedMoreData,
           "decoder is empty after both requests");

    SipStreamDecoder invalid(4096);
    expect(invalid.append("OPTIONS sip:x SIP/2.0\r\nBroken\r\n\r\n", &error),
           "malformed frame fits the buffer");
    expect(invalid.next(decoded, &error) == SipStreamDecoder::InvalidMessage,
           "malformed TCP message is reported as invalid");

    SipStreamDecoder limited(16);
    expect(!limited.append(std::string(17, 'x'), &error),
           "configured stream-size limit is enforced before allocation growth");
}

} // namespace

int main() {
    testRegisterParsing();
    testResponseAndRoundTrip();
    testMalformedMessages();
    testDigestReferenceVector();
    testSipDigestVerification();
    testTcpStreamFraming();

    if (failures != 0) {
        std::cerr << failures << " GB28181 SIP core test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 SIP core tests passed" << std::endl;
    return EXIT_SUCCESS;
}
