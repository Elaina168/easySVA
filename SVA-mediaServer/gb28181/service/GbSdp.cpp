#include "GbSdp.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace easy_sva {
namespace gb28181 {
namespace {

std::string trim(const std::string &value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string upperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
        if (ch >= 'a' && ch <= 'z') {
            return static_cast<char>(ch - 'a' + 'A');
        }
        return ch;
    });
    return value;
}

bool isTwentyDigitId(const std::string &value) {
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

bool parseUnsigned(const std::string &text, uint64_t maximum, uint64_t &value) {
    if (text.empty()) {
        return false;
    }
    uint64_t parsed = 0;
    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        if (*it < '0' || *it > '9') {
            return false;
        }
        const uint64_t digit = static_cast<uint64_t>(*it - '0');
        if (parsed > (maximum - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

bool containsWhitespace(const std::string &value) {
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (std::isspace(static_cast<unsigned char>(*it))) {
            return true;
        }
    }
    return false;
}

} // namespace

GbSdpOffer::GbSdpOffer() : destinationPort(0), tcpPassive(false) {}

GbSdpDescription::GbSdpDescription() : mediaPort(0), direction("sendrecv") {}

bool GbSdpDescription::isTcp() const {
    return upperAscii(transport).find("TCP") != std::string::npos;
}

bool GbSdpDescription::supportsPs() const {
    for (std::map<int, std::string>::const_iterator it = rtpMaps.begin();
         it != rtpMaps.end(); ++it) {
        if (upperAscii(it->second) == "PS/90000") {
            return true;
        }
    }
    return false;
}

void GbSdp::setError(std::string *error, const std::string &value) {
    if (error) {
        *error = value;
    }
}

bool GbSdp::parseSsrc(const std::string &text,
                      uint32_t &ssrc,
                      std::string *error) {
    if (text.size() != 10) {
        setError(error, "GB28181 SSRC must contain exactly 10 decimal digits");
        return false;
    }
    uint64_t parsed = 0;
    if (!parseUnsigned(text, std::numeric_limits<uint32_t>::max(), parsed)) {
        setError(error, "GB28181 SSRC is not a valid 32-bit decimal value");
        return false;
    }
    ssrc = static_cast<uint32_t>(parsed);
    return true;
}

bool GbSdp::buildPlayOffer(const GbSdpOffer &offer,
                           std::string &sdp,
                           std::string *error) {
    if (error) {
        error->clear();
    }
    if (!isTwentyDigitId(offer.platformId)) {
        setError(error, "GB28181 platform ID must contain 20 decimal digits");
        return false;
    }
    if (!isTwentyDigitId(offer.channelId)) {
        setError(error, "GB28181 channel ID must contain 20 decimal digits");
        return false;
    }
    if (offer.destinationIp.empty() || containsWhitespace(offer.destinationIp)) {
        setError(error, "GB28181 SDP destination address is invalid");
        return false;
    }
    if (offer.destinationPort == 0) {
        setError(error, "GB28181 SDP destination port must be positive");
        return false;
    }
    uint32_t numericSsrc = 0;
    if (!parseSsrc(offer.ssrc, numericSsrc, error)) {
        return false;
    }

    const std::string addressType =
        offer.destinationIp.find(':') == std::string::npos ? "IP4" : "IP6";
    const std::string transport = offer.tcpPassive ? "TCP/RTP/AVP" : "RTP/AVP";
    std::ostringstream output;
    output << "v=0\r\n"
           << "o=" << offer.platformId << " 0 0 IN " << addressType << " "
           << offer.destinationIp << "\r\n"
           << "s=Play\r\n"
           << "u=" << offer.channelId << ":0\r\n"
           << "c=IN " << addressType << " " << offer.destinationIp << "\r\n"
           << "t=0 0\r\n"
           << "m=video " << offer.destinationPort << " " << transport << " 96\r\n"
           << "a=recvonly\r\n";
    if (offer.tcpPassive) {
        output << "a=setup:passive\r\n"
               << "a=connection:new\r\n";
    }
    output << "a=rtpmap:96 PS/90000\r\n"
           << "y=" << offer.ssrc << "\r\n";
    sdp = output.str();
    return true;
}

bool GbSdp::parse(const std::string &sdp,
                  GbSdpDescription &description,
                  std::string *error) {
    if (error) {
        error->clear();
    }
    description = GbSdpDescription();
    bool foundVideo = false;
    bool inVideoSection = false;
    std::string sessionConnection;
    std::istringstream input(sdp);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.size() < 2 || line[1] != '=') {
            continue;
        }
        const char field = line[0];
        const std::string value = trim(line.substr(2));
        if (field == 's') {
            description.sessionName = value;
        } else if (field == 'c') {
            std::istringstream connectionLine(value);
            std::string networkType;
            std::string addressType;
            std::string address;
            if (!(connectionLine >> networkType >> addressType >> address) ||
                upperAscii(networkType) != "IN" ||
                (upperAscii(addressType) != "IP4" && upperAscii(addressType) != "IP6")) {
                setError(error, "invalid GB28181 SDP connection line");
                return false;
            }
            if (inVideoSection) {
                description.connectionAddress = address;
            } else {
                sessionConnection = address;
            }
        } else if (field == 'm') {
            std::istringstream mediaLine(value);
            std::string mediaType;
            std::string portText;
            std::string protocol;
            if (!(mediaLine >> mediaType >> portText >> protocol)) {
                setError(error, "invalid GB28181 SDP media line");
                return false;
            }
            inVideoSection = upperAscii(mediaType) == "VIDEO" && !foundVideo;
            if (!inVideoSection) {
                continue;
            }
            foundVideo = true;
            const size_t slash = portText.find('/');
            if (slash != std::string::npos) {
                portText = portText.substr(0, slash);
            }
            uint64_t port = 0;
            if (!parseUnsigned(portText, 65535, port) || port == 0) {
                setError(error, "invalid GB28181 SDP media port");
                return false;
            }
            description.mediaPort = static_cast<uint16_t>(port);
            description.transport = protocol;
            int payloadType = 0;
            while (mediaLine >> payloadType) {
                if (payloadType < 0 || payloadType > 127) {
                    setError(error, "invalid GB28181 SDP payload type");
                    return false;
                }
                description.payloadTypes.push_back(payloadType);
            }
        } else if (field == 'a' && inVideoSection && foundVideo) {
            if (value == "sendonly" || value == "recvonly" ||
                value == "sendrecv" || value == "inactive") {
                description.direction = value;
            } else if (value.compare(0, 6, "setup:") == 0) {
                description.setup = trim(value.substr(6));
            } else if (value.compare(0, 11, "connection:") == 0) {
                description.connection = trim(value.substr(11));
            } else if (value.compare(0, 7, "rtpmap:") == 0) {
                const std::string mapping = value.substr(7);
                const size_t space = mapping.find_first_of(" \t");
                if (space == std::string::npos) {
                    setError(error, "invalid GB28181 SDP rtpmap attribute");
                    return false;
                }
                uint64_t payload = 0;
                if (!parseUnsigned(mapping.substr(0, space), 127, payload)) {
                    setError(error, "invalid GB28181 SDP rtpmap payload type");
                    return false;
                }
                description.rtpMaps[static_cast<int>(payload)] = trim(mapping.substr(space + 1));
            }
        } else if (field == 'y') {
            description.ssrc = value;
        }
    }

    if (!foundVideo) {
        setError(error, "GB28181 SDP does not contain a video media section");
        return false;
    }
    if (description.connectionAddress.empty()) {
        description.connectionAddress = sessionConnection;
    }
    if (description.connectionAddress.empty()) {
        setError(error, "GB28181 SDP does not contain a connection address");
        return false;
    }
    uint32_t numericSsrc = 0;
    if (!parseSsrc(description.ssrc, numericSsrc, error)) {
        return false;
    }
    return true;
}

} // namespace gb28181
} // namespace easy_sva
