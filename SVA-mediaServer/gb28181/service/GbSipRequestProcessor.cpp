#include "GbSipRequestProcessor.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>

#include "core/DigestAuth.h"
#include "core/SipResponse.h"
#include "GbXmlParser.h"

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

bool requiredMessageHeadersPresent(const SipMessage &request) {
    return request.hasHeader("Via") && request.hasHeader("From") &&
           request.hasHeader("To") && request.hasHeader("Call-ID") &&
           request.hasHeader("CSeq") && request.hasHeader("Content-Type");
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
      _catalogs(new DeviceCatalogStore()),
      _clock(gbSipUnixSeconds),
      _fallback(config.serverId) {}

GbSipRequestProcessor::GbSipRequestProcessor(const GbSipConfig &config,
                                             const RegistrationStore::Ptr &registrations,
                                             const DigestNonceStore::Ptr &nonces,
                                             const Clock &clock)
    : GbSipRequestProcessor(config, registrations, nonces,
                            DeviceCatalogStore::Ptr(new DeviceCatalogStore()), clock) {}

GbSipRequestProcessor::GbSipRequestProcessor(const GbSipConfig &config,
                                             const RegistrationStore::Ptr &registrations,
                                             const DigestNonceStore::Ptr &nonces,
                                             const DeviceCatalogStore::Ptr &catalogs,
                                             const Clock &clock)
    : _config(config),
      _registrations(registrations),
      _nonces(nonces),
      _catalogs(catalogs),
      _clock(clock),
      _fallback(config.serverId) {
    if (!_registrations) {
        _registrations.reset(new RegistrationStore());
    }
    if (!_nonces) {
        _nonces.reset(new DigestNonceStore(config.nonceTtlSeconds));
    }
    if (!_catalogs) {
        _catalogs.reset(new DeviceCatalogStore());
    }
    if (!_clock) {
        _clock = gbSipUnixSeconds;
    }
}

void GbSipRequestProcessor::sweep() {
    const uint64_t now = _clock();
    _registrations->expire(now);
    _registrations->markHeartbeatTimeouts(now, _config.heartbeatTimeoutSeconds);
}

bool GbSipRequestProcessor::process(const SipMessage &message,
                                    const SipPeer &peer,
                                    SipMessage &response) {
    sweep();
    if (!message.isRequest()) {
        if (_response_handler) {
            _response_handler(message, peer);
        }
        return false;
    }
    if (message.isRequest() && message.method() == "REGISTER") {
        return processRegister(message, peer, response);
    }
    if (message.isRequest() && message.method() == "MESSAGE") {
        return processMessage(message, peer, response);
    }
    return _fallback.process(message, peer, response);
}

void GbSipRequestProcessor::setResponseHandler(const ResponseHandler &handler) {
    _response_handler = handler;
}

const RegistrationStore::Ptr &GbSipRequestProcessor::registrations() const {
    return _registrations;
}

const DigestNonceStore::Ptr &GbSipRequestProcessor::nonces() const {
    return _nonces;
}

const DeviceCatalogStore::Ptr &GbSipRequestProcessor::catalogs() const {
    return _catalogs;
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
        device.lastHeartbeatAt = now;
        device.online = true;
        device.sender = peer.sender;
        _registrations->upsert(device);
    }

    if (expires == 0) {
        _catalogs->removeDevice(deviceId);
    }

    response = SipResponse::fromRequest(request, 200, "OK", _config.serverId);
    response.addHeader("Contact", contact);
    response.addHeader("Expires", std::to_string(expires));
    return true;
}

bool GbSipRequestProcessor::processMessage(const SipMessage &request,
                                           const SipPeer &peer,
                                           SipMessage &response) {
    if (!requiredMessageHeadersPresent(request) || request.body().empty()) {
        response = registerError(request, 400, "Bad Request", _config.serverId);
        return true;
    }
    const std::string contentType = lowerAscii(request.header("Content-Type"));
    if (contentType.find("application/manscdp+xml") != 0) {
        response = registerError(request, 415, "Unsupported Media Type", _config.serverId);
        return true;
    }

    GbXmlMessage xml;
    std::string xmlError;
    if (!GbXmlParser::parse(request.body(), xml, &xmlError)) {
        response = registerError(request, 400, "Invalid MANSCDP XML", _config.serverId);
        return true;
    }
    if (!isDeviceId(xml.deviceId) || sipUser(request.header("From")) != xml.deviceId) {
        response = registerError(request, 403, "Forbidden", _config.serverId);
        return true;
    }

    RegisteredDevice registered;
    if (!_registrations->find(xml.deviceId, registered)) {
        response = registerError(request, 403, "Device Not Registered", _config.serverId);
        return true;
    }

    const std::string command = lowerAscii(xml.command);
    const uint64_t now = _clock();
    if (command == "keepalive") {
        if (!xml.status.empty() && lowerAscii(xml.status) != "ok") {
            response = registerError(request, 400, "Invalid Keepalive Status", _config.serverId);
            return true;
        }
        _registrations->touchHeartbeat(
            xml.deviceId, now, peer.ip, peer.port, peer.transport, peer.sender);
        response = SipResponse::fromRequest(request, 200, "OK", _config.serverId);
        return true;
    }

    if (command == "catalog") {
        std::vector<DeviceChannel> channels;
        channels.reserve(xml.catalogItems.size());
        for (std::vector<GbCatalogItem>::const_iterator it = xml.catalogItems.begin();
             it != xml.catalogItems.end(); ++it) {
            if (!isDeviceId(it->deviceId)) {
                response = registerError(request, 400, "Invalid Catalog Device ID", _config.serverId);
                return true;
            }
            DeviceChannel channel;
            channel.deviceId = it->deviceId;
            channel.name = it->name;
            channel.manufacturer = it->manufacturer;
            channel.model = it->model;
            channel.owner = it->owner;
            channel.civilCode = it->civilCode;
            channel.address = it->address;
            channel.parental = it->parental;
            channel.parentId = it->parentId;
            channel.safetyWay = it->safetyWay;
            channel.registerWay = it->registerWay;
            channel.secrecy = it->secrecy;
            channel.status = it->status;
            channel.longitude = it->longitude;
            channel.latitude = it->latitude;
            channels.push_back(channel);
        }
        if (xml.hasSumNum && xml.sumNum == channels.size()) {
            _catalogs->replace(xml.deviceId, channels, now);
        } else {
            _catalogs->upsert(xml.deviceId, channels, now);
        }
        _registrations->touchHeartbeat(
            xml.deviceId, now, peer.ip, peer.port, peer.transport, peer.sender);
        response = SipResponse::fromRequest(request, 200, "OK", _config.serverId);
        return true;
    }

    response = registerError(request, 501, "MANSCDP Command Not Implemented", _config.serverId);
    return true;
}

} // namespace gb28181
} // namespace easy_sva
