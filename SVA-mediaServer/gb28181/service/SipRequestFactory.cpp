#include "SipRequestFactory.h"

#include <algorithm>

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

} // namespace gb28181
} // namespace easy_sva
