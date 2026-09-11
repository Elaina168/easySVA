#include "SipResponse.h"

#include <algorithm>

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

std::string withServerTag(const std::string &to, const std::string &serverTag) {
    if (to.empty() || serverTag.empty() || lowerAscii(to).find(";tag=") != std::string::npos) {
        return to;
    }
    return to + ";tag=" + serverTag;
}

void copyHeader(const SipMessage &request, SipMessage &response, const std::string &name) {
    const std::string value = request.header(name);
    if (!value.empty()) {
        response.addHeader(name, value);
    }
}

} // namespace

SipMessage SipResponse::fromRequest(const SipMessage &request,
                                    int statusCode,
                                    const std::string &reasonPhrase,
                                    const std::string &serverTag,
                                    const std::string &body,
                                    const std::string &contentType) {
    SipMessage response;
    response.setStatusLine(statusCode, reasonPhrase);

    const std::vector<std::string> vias = request.headerValues("Via");
    for (std::vector<std::string>::const_iterator it = vias.begin(); it != vias.end(); ++it) {
        response.addHeader("Via", *it);
    }
    copyHeader(request, response, "From");

    const std::string to = request.header("To");
    if (!to.empty()) {
        response.addHeader("To", statusCode > 100 ? withServerTag(to, serverTag) : to);
    }
    copyHeader(request, response, "Call-ID");
    copyHeader(request, response, "CSeq");
    response.addHeader("Server", "easySVA-GB28181/1.0");

    if (!body.empty() && !contentType.empty()) {
        response.addHeader("Content-Type", contentType);
    }
    response.setBody(body);
    return response;
}

SipMessage SipResponse::badRequest() {
    SipMessage response;
    response.setStatusLine(400, "Bad Request");
    response.addHeader("Server", "easySVA-GB28181/1.0");
    return response;
}

} // namespace gb28181
} // namespace easy_sva
