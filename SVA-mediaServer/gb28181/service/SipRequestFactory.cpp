#include "SipRequestFactory.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace easy_sva {
namespace gb28181 {
namespace {

std::string upperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
        if (ch >= 'a' && ch <= 'z') {
            return static_cast<char>(ch - 'a' + 'A');
        }
        return ch;
    });
    return value;
}

std::string sipUri(const std::string &id, const std::string &realm) {
    return "sip:" + id + "@" + realm;
}

std::string peerHost(const std::string &address) {
    if (address.find(':') != std::string::npos &&
        (address.empty() || address[0] != '[')) {
        return "[" + address + "]";
    }
    return address;
}

std::string deviceRequestUri(const GbSipConfig &config,
                             const RegisteredDevice &device,
                             const std::string &channelId) {
    if (device.peerIp.empty()) {
        return sipUri(channelId, config.realm);
    }
    std::string uri = "sip:" + channelId + "@" + peerHost(device.peerIp);
    if (device.peerPort != 0) {
        uri += ":" + std::to_string(device.peerPort);
    }
    return uri;
}

std::string firstSipUri(const std::string &header) {
    const std::string lowered = [&header]() {
        std::string value = header;
        std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
            if (ch >= 'A' && ch <= 'Z') {
                return static_cast<char>(ch - 'A' + 'a');
            }
            return ch;
        });
        return value;
    }();
    const size_t begin = lowered.find("sip:");
    if (begin == std::string::npos) {
        return std::string();
    }
    size_t end = header.find('>', begin);
    if (end == std::string::npos) {
        end = header.find_first_of(" \t,", begin);
    }
    return header.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

std::string cseqNumber(const SipMessage &message) {
    std::istringstream input(message.header("CSeq"));
    std::string sequence;
    input >> sequence;
    return sequence;
}

std::string inviteTransport(const SipMessage &invite) {
    const std::string via = invite.header("Via");
    const std::string prefix = "SIP/2.0/";
    if (via.compare(0, prefix.size(), prefix) != 0) {
        return "UDP";
    }
    const size_t end = via.find_first_of(" \t", prefix.size());
    return via.substr(prefix.size(), end == std::string::npos
        ? std::string::npos : end - prefix.size());
}

std::string newVia(const GbSipConfig &config,
                   const std::string &transport,
                   const std::string &token) {
    return "SIP/2.0/" + transport + " " + config.advertisedIp + ":" +
        std::to_string(config.sipPort) + ";branch=z9hG4bK-" + token;
}

std::vector<std::string> dialogRouteSet(const SipMessage &response) {
    std::vector<std::string> routes = response.headerValues("Record-Route");
    std::reverse(routes.begin(), routes.end());
    return routes;
}

std::string remoteTarget(const SipMessage &invite,
                         const SipMessage &acceptedResponse) {
    const std::string contact = firstSipUri(acceptedResponse.header("Contact"));
    return contact.empty() ? invite.requestUri() : contact;
}

void addDialogRoutes(SipMessage &request,
                     const std::vector<std::string> &routes) {
    for (std::vector<std::string>::const_iterator it = routes.begin();
         it != routes.end(); ++it) {
        request.addHeader("Route", *it);
    }
}

void addDialogIdentity(SipMessage &request,
                       const SipMessage &invite,
                       const SipMessage &response,
                       const std::string &cseq) {
    request.addHeader("From", invite.header("From"));
    request.addHeader("To", response.header("To"));
    request.addHeader("Call-ID", invite.header("Call-ID"));
    request.addHeader("CSeq", cseq);
    request.addHeader("Max-Forwards", "70");
}

} // namespace

SipMessage SipRequestFactory::catalogQuery(const GbSipConfig &config,
                                           const RegisteredDevice &device,
                                           uint64_t serialNumber,
                                           uint64_t cseq,
                                           const std::string &token) {
    const std::string deviceUri = sipUri(device.deviceId, config.realm);
    const std::string platformUri = sipUri(config.serverId, config.realm);
    const std::string transport = upperAscii(device.transport.empty() ? "UDP" : device.transport);

    SipMessage request;
    request.setRequestLine("MESSAGE", deviceUri);
    request.addHeader("Via", "SIP/2.0/" + transport + " " + config.advertisedIp + ":" +
        std::to_string(config.sipPort) + ";branch=z9hG4bK-" + token);
    request.addHeader("From", "<" + platformUri + ">;tag=" + token);
    request.addHeader("To", "<" + deviceUri + ">");
    request.addHeader("Call-ID", token + "@" + config.advertisedIp);
    request.addHeader("CSeq", std::to_string(cseq) + " MESSAGE");
    request.addHeader("Max-Forwards", "70");
    request.addHeader("Contact", "<sip:" + config.serverId + "@" + config.advertisedIp + ":" +
        std::to_string(config.sipPort) + ">");
    request.addHeader("Content-Type", "Application/MANSCDP+xml");
    request.setBody("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                    "<Query>\r\n"
                    "<CmdType>Catalog</CmdType>\r\n"
                    "<SN>" + std::to_string(serialNumber) + "</SN>\r\n"
                    "<DeviceID>" + device.deviceId + "</DeviceID>\r\n"
                    "</Query>\r\n");
    return request;
}

SipMessage SipRequestFactory::liveInvite(const GbSipConfig &config,
                                         const RegisteredDevice &device,
                                         const std::string &channelId,
                                         const std::string &sdp,
                                         const std::string &ssrc,
                                         uint64_t cseq,
                                         const std::string &token) {
    const std::string channelUri = sipUri(channelId, config.realm);
    const std::string platformUri = sipUri(config.serverId, config.realm);
    const std::string transport = upperAscii(device.transport.empty() ? "UDP" : device.transport);

    SipMessage request;
    request.setRequestLine("INVITE", deviceRequestUri(config, device, channelId));
    request.addHeader("Via", newVia(config, transport, token));
    request.addHeader("From", "<" + platformUri + ">;tag=" + token);
    request.addHeader("To", "<" + channelUri + ">");
    request.addHeader("Call-ID", token + "@" + config.advertisedIp);
    request.addHeader("CSeq", std::to_string(cseq) + " INVITE");
    request.addHeader("Max-Forwards", "70");
    request.addHeader("Contact", "<sip:" + config.serverId + "@" +
        config.advertisedIp + ":" + std::to_string(config.sipPort) + ">");
    request.addHeader("Subject", channelId + ":" + ssrc + "," +
        config.serverId + ":0");
    request.addHeader("Content-Type", "application/sdp");
    request.setBody(sdp);
    return request;
}

SipMessage SipRequestFactory::inviteAck(const GbSipConfig &config,
                                        const SipMessage &invite,
                                        const SipMessage &finalResponse,
                                        const std::string &token) {
    const bool accepted = finalResponse.statusCode() >= 200 &&
        finalResponse.statusCode() < 300;
    const std::string requestUri = accepted
        ? remoteTarget(invite, finalResponse) : invite.requestUri();

    SipMessage request;
    request.setRequestLine("ACK", requestUri);
    if (accepted) {
        request.addHeader("Via", newVia(config, inviteTransport(invite), token));
    } else {
        request.addHeader("Via", invite.header("Via"));
    }
    addDialogIdentity(request, invite, finalResponse,
                      cseqNumber(invite) + " ACK");
    if (accepted) {
        addDialogRoutes(request, dialogRouteSet(finalResponse));
    }
    return request;
}

SipMessage SipRequestFactory::dialogBye(const GbSipConfig &config,
                                        const SipMessage &invite,
                                        const SipMessage &acceptedResponse,
                                        uint64_t cseq,
                                        const std::string &token) {
    SipMessage request;
    request.setRequestLine("BYE", remoteTarget(invite, acceptedResponse));
    request.addHeader("Via", newVia(config, inviteTransport(invite), token));
    addDialogIdentity(request, invite, acceptedResponse,
                      std::to_string(cseq) + " BYE");
    addDialogRoutes(request, dialogRouteSet(acceptedResponse));
    return request;
}

} // namespace gb28181
} // namespace easy_sva
