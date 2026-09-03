#include "GbSipRequestProcessor.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>

#include "core/DigestAuth.h"
#include "core/SipResponse.h"

namespace easy_sva {
namespace gb28181 {
namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return ch;
    });
    return value;
}

bool isDeviceId(const std::string &value) {
    if (value.size() != 20) {
        return false;
    }
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (*it < '0' || *it > '9') {
            return false;
        }
    }
    return true;
}

std::string sipUser(const std::string &header) {
    const std::string lowered = lowerAscii(header);
    const size_t scheme = lowered.find("sip:");
    if (scheme == std::string::npos) {
        return std::string();
    }
    const size_t begin = scheme + 4;
    size_t end = begin;
    while (end < header.size() && header[end] != '@' && header[end] != ';' &&
           header[end] != '>' && header[end] != ' ' && header[end] != '\t') {
        ++end;
    }
    return header.substr(begin, end - begin);
}

bool parseUnsigned(const std::string &text, uint32_t &value) {
    if (text.empty() || text[0] == '-') {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != '\0' ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

std::string parameterValue(const std::string &header, const std::string &name) {
    const std::string lowered = lowerAscii(header);
    const std::string needle = ";" + lowerAscii(name) + "=";
    const size_t parameter = lowered.find(needle);
    if (parameter == std::string::npos) {
        return std::string();
    }
    const size_t begin = parameter + needle.size();
    size_t end = begin;
    while (end < header.size() && header[end] != ';' && header[end] != ',' &&
           header[end] != ' ' && header[end] != '\t' && header[end] != '>') {
        ++end;
    }
    return header.substr(begin, end - begin);
}

bool requiredRegisterHeadersPresent(const SipMessage &request) {
    return request.hasHeader("Via") && request.hasHeader("From") &&
           request.hasHeader("To") && request.hasHeader("Call-ID") &&
           request.hasHeader("CSeq") && request.hasHeader("Contact");
}

SipMessage registerError(const SipMessage &request,
                         int status,
                         const std::string &reason,
                         const std::string &serverTag) {
    return SipResponse::fromRequest(request, status, reason, serverTag);
}

} // namespace

GbSipRequestProcessor::GbSipRequestProcessor(const GbSipConfig &config)
    : _config(config),
      _registrations(new RegistrationStore()),
      _nonces(new DigestNonceStore(config.nonceTtlSeconds)),
      _clock(gbSipUnixSeconds),
      _fallback(config.serverId) {}

GbSipRequestProcessor::GbSipRequestProcessor(const GbSipConfig &config,
                                             const RegistrationStore::Ptr &registrations,
                                             const DigestNonceStore::Ptr &nonces,
                                             const Clock &clock)
    : _config(config),
      _registrations(registrations),
      _nonces(nonces),
      _clock(clock),
      _fallback(config.serverId) {
    if (!_registrations) {
        _registrations.reset(new RegistrationStore());
    }
    if (!_nonces) {
        _nonces.reset(new DigestNonceStore(config.nonceTtlSeconds));
    }
    if (!_clock) {
        _clock = gbSipUnixSeconds;
    }
}

bool GbSipRequestProcessor::process(const SipMessage &message,
                                    const SipPeer &peer,
                                    SipMessage &response) {
    const uint64_t now = _clock();
    _registrations->expire(now);
    if (message.isRequest() && message.method() == "REGISTER") {
        return processRegister(message, peer, response);
    }
    return _fallback.process(message, peer, response);
}

const RegistrationStore::Ptr &GbSipRequestProcessor::registrations() const {
    return _registrations;
}

const DigestNonceStore::Ptr &GbSipRequestProcessor::nonces() const {
    return _nonces;
}

bool GbSipRequestProcessor::challenge(const SipMessage &request,
                                      const std::string &deviceId,
                                      bool stale,
                                      SipMessage &response) {
    _nonces->prune();
    response = SipResponse::fromRequest(request, 401, "Unauthorized", _config.serverId);
    response.addHeader("WWW-Authenticate",
        DigestAuth::buildChallenge(_config.realm, _nonces->issue(deviceId), stale));
    return true;
}

bool GbSipRequestProcessor::processRegister(const SipMessage &request,
                                            const SipPeer &peer,
                                            SipMessage &response) {
    if (!requiredRegisterHeadersPresent(request)) {
        response = registerError(request, 400, "Bad Request", _config.serverId);
        return true;
    }

    const std::string deviceId = sipUser(request.header("From"));
    if (!isDeviceId(deviceId)) {
        response = registerError(request, 400, "Invalid GB28181 Device ID", _config.serverId);
        return true;
    }

    if (_config.authRequired) {
        const std::string authorization = request.header("Authorization");
        if (authorization.empty()) {
            return challenge(request, deviceId, false, response);
        }

        DigestCredentials credentials;
        std::string authError;
        if (!DigestAuth::parseCredentials(authorization, credentials, &authError) ||
            credentials.username != deviceId || credentials.realm != _config.realm) {
            response = registerError(request, 403, "Forbidden", _config.serverId);
            return true;
        }

        const DigestNonceStore::Status nonceStatus = _nonces->validate(deviceId, credentials.nonce);
        if (nonceStatus != DigestNonceStore::Valid) {
            return challenge(request, deviceId, nonceStatus == DigestNonceStore::Stale, response);
        }
        if (!DigestAuth::verifyRequest(request, _config.devicePassword, _config.realm,
                                       credentials.nonce, &authError)) {
            response = registerError(request, 403, "Forbidden", _config.serverId);
            return true;
        }
    }

    const std::string contact = request.header("Contact");
    uint32_t expires = _config.defaultRegisterExpires;
    const std::string expiresHeader = request.header("Expires");
    const std::string contactExpires = parameterValue(contact, "expires");
    if (!expiresHeader.empty()) {
        if (!parseUnsigned(expiresHeader, expires)) {
            response = registerError(request, 400, "Invalid Expires", _config.serverId);
            return true;
        }
    } else if (!contactExpires.empty() && !parseUnsigned(contactExpires, expires)) {
        response = registerError(request, 400, "Invalid Contact Expires", _config.serverId);
        return true;
    }

    if (expires != 0 && expires < _config.minRegisterExpires) {
        response = registerError(request, 423, "Interval Too Brief", _config.serverId);
        response.addHeader("Min-Expires", std::to_string(_config.minRegisterExpires));
        return true;
    }
    if (expires > _config.maxRegisterExpires) {
        expires = _config.maxRegisterExpires;
    }

    if (expires == 0) {
        _registrations->remove(deviceId);
    } else {
        const uint64_t now = _clock();
        RegisteredDevice device;
        device.deviceId = deviceId;
        device.contact = contact;
        device.transport = peer.transport;
        device.peerIp = peer.ip;
        device.peerPort = peer.port;
        device.userAgent = request.header("User-Agent");
        device.callId = request.header("Call-ID");
        device.registeredAt = now;
        device.lastRegisterAt = now;
        device.expiresAt = now + expires;
        _registrations->upsert(device);
    }

    response = SipResponse::fromRequest(request, 200, "OK", _config.serverId);
    response.addHeader("Contact", contact);
    response.addHeader("Expires", std::to_string(expires));
    return true;
}

} // namespace gb28181
} // namespace easy_sva
