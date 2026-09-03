#ifndef EASY_SVA_GB28181_GB_SIP_CONFIG_H
#define EASY_SVA_GB28181_GB_SIP_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace easy_sva {
namespace gb28181 {

struct GbSipConfig {
    std::string serverId;
    std::string realm;
    std::string listenIp;
    uint16_t sipPort;
    bool enableUdp;
    bool enableTcp;
    uint32_t idleTimeoutSeconds;
    bool authRequired;
    std::string devicePassword;
    uint32_t nonceTtlSeconds;
    uint32_t defaultRegisterExpires;
    uint32_t minRegisterExpires;
    uint32_t maxRegisterExpires;
    size_t maxMessageBytes;

    GbSipConfig();

    static bool parse(const std::string &text,
                      GbSipConfig &config,
                      std::string *error = nullptr);

    static bool load(const std::string &path,
                     GbSipConfig &config,
                     std::string *error = nullptr);

    bool validate(std::string *error = nullptr) const;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_GB_SIP_CONFIG_H
