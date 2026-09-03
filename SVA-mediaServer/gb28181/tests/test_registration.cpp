#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "core/DigestAuth.h"
#include "service/DigestNonceStore.h"
#include "service/GbSipRequestProcessor.h"
#include "service/RegistrationStore.h"

using easy_sva::gb28181::DigestAuth;
using easy_sva::gb28181::DigestCredentials;
using easy_sva::gb28181::DigestNonceStore;
using easy_sva::gb28181::GbSipConfig;
using easy_sva::gb28181::GbSipRequestProcessor;
using easy_sva::gb28181::RegisteredDevice;
using easy_sva::gb28181::RegistrationStore;
using easy_sva::gb28181::SipMessage;
using easy_sva::gb28181::SipPeer;

namespace {

const char *deviceId = "34020000001320000001";
const char *requestUri = "sip:34020000002000000001@3402000000";
int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

SipMessage makeRegister(uint32_t expires, int cseq, const std::string &contact) {
    SipMessage request;
    request.setRequestLine("REGISTER", requestUri);
    request.addHeader("Via", "SIP/2.0/UDP 192.0.2.15:5060;branch=z9hG4bK-register");
    request.addHeader("From", std::string("<sip:") + deviceId + "@3402000000>;tag=device-tag");
    request.addHeader("To", std::string("<sip:") + deviceId + "@3402000000>");
    request.addHeader("Call-ID", "register-call");
    request.addHeader("CSeq", std::to_string(cseq) + " REGISTER");
    request.addHeader("Contact", contact);
    request.addHeader("Expires", std::to_string(expires));
    request.addHeader("User-Agent", "easySVA-test-device/1.0");
    return request;
}

void authorize(SipMessage &request,
               const std::string &nonce,
               const std::string &password,
               const std::string &nonceCount = "00000001") {
    DigestCredentials credentials;
    credentials.username = deviceId;
    credentials.realm = "3402000000";
    credentials.nonce = nonce;
    credentials.uri = requestUri;
    credentials.algorithm = "MD5";
    credentials.qop = "auth";
    credentials.nonceCount = nonceCount;
    credentials.clientNonce = "test-cnonce";
    const std::string digest = DigestAuth::computeResponse("REGISTER", password, credentials);
    request.addHeader("Authorization",
        "Digest username=\"" + credentials.username + "\", realm=\"" + credentials.realm +
        "\", nonce=\"" + credentials.nonce + "\", uri=\"" + credentials.uri +
        "\", response=\"" + digest + "\", algorithm=MD5, qop=auth, nc=" +
        credentials.nonceCount + ", cnonce=\"" + credentials.clientNonce + "\"");
}

struct Fixture {
    uint64_t now;
    int nonceSequence;
    GbSipConfig config;
    RegistrationStore::Ptr registrations;
    DigestNonceStore::Ptr nonces;
    std::shared_ptr<GbSipRequestProcessor> processor;
    SipPeer peer;

    Fixture() : now(1000), nonceSequence(0), registrations(new RegistrationStore()) {
        config.devicePassword = "test-password";
        config.nonceTtlSeconds = 300;
        config.defaultRegisterExpires = 3600;
        config.minRegisterExpires = 60;
        config.maxRegisterExpires = 86400;
        DigestNonceStore::Clock clock = [this]() { return now; };
        DigestNonceStore::NonceFactory factory = [this]() {
            return std::string("nonce-") + std::to_string(++nonceSequence);
        };
        nonces.reset(new DigestNonceStore(config.nonceTtlSeconds, clock, factory));
        processor.reset(new GbSipRequestProcessor(config, registrations, nonces, clock));
        peer.transport = "UDP";
        peer.ip = "192.0.2.15";
        peer.port = 5060;
    }

    std::string challengeNonce(const SipMessage &request, SipMessage &response) {
        expect(processor->process(request, peer, response), "REGISTER challenge produces a response");
        expect(response.statusCode() == 401, "first REGISTER receives a 401 challenge");
        const std::string challenge = response.header("WWW-Authenticate");
        const std::string marker = "nonce=\"";
        const size_t begin = challenge.find(marker);
        if (begin == std::string::npos) {
            expect(false, "challenge contains a nonce");
            return std::string();
        }
        const size_t valueBegin = begin + marker.size();
        const size_t end = challenge.find('"', valueBegin);
        expect(end != std::string::npos, "challenge nonce is quoted");
        return end == std::string::npos ? std::string() : challenge.substr(valueBegin, end - valueBegin);
    }
};

void testChallengeAndRegistration() {
    Fixture fixture;
    const std::string contact = std::string("<sip:") + deviceId + "@192.0.2.15:5060>";
    SipMessage request = makeRegister(3600, 1, contact);
    SipMessage response;
    const std::string nonce = fixture.challengeNonce(request, response);

    request = makeRegister(3600, 2, contact);
    authorize(request, nonce, fixture.config.devicePassword);
    expect(fixture.processor->process(request, fixture.peer, response), "authenticated REGISTER responds");
    expect(response.statusCode() == 200, "authenticated REGISTER returns 200");
    expect(response.header("Expires") == "3600", "accepted expiry is returned");
    expect(fixture.registrations->size() == 1, "registered device is stored");

    RegisteredDevice registered;
    expect(fixture.registrations->find(deviceId, registered), "device can be looked up by ID");
    expect(registered.peerIp == "192.0.2.15" && registered.peerPort == 5060,
           "signaling source endpoint is stored");
    expect(registered.contact == contact && registered.expiresAt == 4600,
           "contact and absolute expiry are stored");
}

void testWrongPasswordAndStaleNonce() {
    Fixture fixture;
    const std::string contact = std::string("<sip:") + deviceId + "@192.0.2.15:5060>";
    SipMessage response;
    SipMessage request = makeRegister(3600, 1, contact);
    const std::string nonce = fixture.challengeNonce(request, response);

    request = makeRegister(3600, 2, contact);
    authorize(request, nonce, "wrong-password");
    fixture.processor->process(request, fixture.peer, response);
    expect(response.statusCode() == 403, "wrong Digest password returns 403");
    expect(fixture.registrations->size() == 0, "wrong password never creates a registration");

    fixture.now += fixture.config.nonceTtlSeconds;
    request = makeRegister(3600, 3, contact);
    authorize(request, nonce, fixture.config.devicePassword);
    fixture.processor->process(request, fixture.peer, response);
    expect(response.statusCode() == 401, "expired nonce receives a fresh challenge");
    expect(response.header("WWW-Authenticate").find("stale=true") != std::string::npos,
           "expired nonce is marked stale");
}

void testRefreshLogoutAndExpiry() {
    Fixture fixture;
    const std::string firstContact = std::string("<sip:") + deviceId + "@192.0.2.15:5060>";
    SipMessage response;
    SipMessage request = makeRegister(120, 1, firstContact);
    const std::string nonce = fixture.challengeNonce(request, response);
    authorize(request, nonce, fixture.config.devicePassword);
    fixture.processor->process(request, fixture.peer, response);

    fixture.now += 30;
    fixture.peer.ip = "198.51.100.22";
    fixture.peer.port = 15060;
    const std::string newContact = std::string("<sip:") + deviceId + "@198.51.100.22:15060>";
    request = makeRegister(300, 2, newContact);
    authorize(request, nonce, fixture.config.devicePassword, "00000002");
    fixture.processor->process(request, fixture.peer, response);
    expect(response.statusCode() == 200, "registration refresh returns 200");

    RegisteredDevice registered;
    fixture.registrations->find(deviceId, registered);
    expect(registered.registeredAt == 1000 && registered.lastRegisterAt == 1030,
           "refresh preserves first registration and updates last registration time");
    expect(registered.peerIp == "198.51.100.22" && registered.contact == newContact,
           "refresh replaces the signaling endpoint and Contact");

    request = makeRegister(0, 3, newContact);
    authorize(request, nonce, fixture.config.devicePassword, "00000003");
    fixture.processor->process(request, fixture.peer, response);
    expect(response.statusCode() == 200, "Expires zero logout returns 200");
    expect(fixture.registrations->size() == 0, "Expires zero removes the registration");

    request = makeRegister(60, 4, firstContact);
    authorize(request, nonce, fixture.config.devicePassword, "00000004");
    fixture.processor->process(request, fixture.peer, response);
    fixture.now += 60;
    expect(fixture.registrations->expire(fixture.now) == 1,
           "registration is removed exactly at its expiry time");
}

void testExpiryBounds() {
    Fixture fixture;
    const std::string contact = std::string("<sip:") + deviceId + "@192.0.2.15:5060>";
    SipMessage response;
    SipMessage request = makeRegister(30, 1, contact);
    const std::string nonce = fixture.challengeNonce(request, response);
    authorize(request, nonce, fixture.config.devicePassword);
    fixture.processor->process(request, fixture.peer, response);
    expect(response.statusCode() == 423, "too-short registration returns 423");
    expect(response.header("Min-Expires") == "60", "423 advertises Min-Expires");

    request = makeRegister(999999, 2, contact);
    authorize(request, nonce, fixture.config.devicePassword, "00000002");
    fixture.processor->process(request, fixture.peer, response);
    expect(response.statusCode() == 200, "too-long registration is accepted with a cap");
    expect(response.header("Expires") == "86400", "registration expiry is capped");
}

} // namespace

int main() {
    testChallengeAndRegistration();
    testWrongPasswordAndStaleNonce();
    testRefreshLogoutAndExpiry();
    testExpiryBounds();

    if (failures != 0) {
        std::cerr << failures << " GB28181 registration test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 REGISTER/Digest tests passed" << std::endl;
    return EXIT_SUCCESS;
}
