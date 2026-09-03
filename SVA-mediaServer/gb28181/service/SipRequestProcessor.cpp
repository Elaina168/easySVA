#include "SipRequestProcessor.h"

#include "core/SipResponse.h"

namespace easy_sva {
namespace gb28181 {

SipPeer::SipPeer() : port(0) {}

BasicSipRequestProcessor::BasicSipRequestProcessor(const std::string &serverTag)
    : _server_tag(serverTag) {}

const char *BasicSipRequestProcessor::allowedMethods() {
    return "REGISTER, MESSAGE, INVITE, ACK, BYE, OPTIONS";
}

bool BasicSipRequestProcessor::process(const SipMessage &message,
                                       const SipPeer &peer,
                                       SipMessage &response) {
    (void)peer;
    if (!message.isRequest()) {
        return false;
    }

    const std::string method = message.method();
    if (method == "ACK") {
        return false;
    }
    if (method == "OPTIONS") {
        response = SipResponse::fromRequest(message, 200, "OK", _server_tag);
        response.addHeader("Allow", allowedMethods());
        response.addHeader("Accept", "Application/MANSCDP+xml, application/sdp");
        return true;
    }
    if (method == "REGISTER" || method == "MESSAGE" || method == "INVITE" ||
        method == "BYE") {
        response = SipResponse::fromRequest(message, 501, "Not Implemented", _server_tag);
        return true;
    }

    response = SipResponse::fromRequest(message, 405, "Method Not Allowed", _server_tag);
    response.addHeader("Allow", allowedMethods());
    return true;
}

} // namespace gb28181
} // namespace easy_sva
