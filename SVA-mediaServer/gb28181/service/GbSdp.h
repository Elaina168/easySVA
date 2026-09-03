#ifndef EASY_SVA_GB28181_GB_SDP_H
#define EASY_SVA_GB28181_GB_SDP_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace easy_sva {
namespace gb28181 {

struct GbSdpOffer {
    std::string platformId;
    std::string channelId;
    std::string destinationIp;
    uint16_t destinationPort;
    std::string ssrc;
    bool tcpPassive;

    GbSdpOffer();
};

struct GbSdpDescription {
    std::string sessionName;
    std::string connectionAddress;
    uint16_t mediaPort;
    std::string transport;
    std::vector<int> payloadTypes;
    std::map<int, std::string> rtpMaps;
    std::string direction;
    std::string setup;
    std::string connection;
    std::string ssrc;

    GbSdpDescription();
    bool isTcp() const;
    bool supportsPs() const;
};

class GbSdp {
public:
    static bool buildPlayOffer(const GbSdpOffer &offer,
                               std::string &sdp,
                               std::string *error = nullptr);
    static bool parse(const std::string &sdp,
                      GbSdpDescription &description,
                      std::string *error = nullptr);
    static bool parseSsrc(const std::string &text,
                          uint32_t &ssrc,
                          std::string *error = nullptr);

private:
    static void setError(std::string *error, const std::string &value);
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_GB_SDP_H
