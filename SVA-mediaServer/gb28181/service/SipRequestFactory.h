#ifndef EASY_SVA_GB28181_SIP_REQUEST_FACTORY_H
#define EASY_SVA_GB28181_SIP_REQUEST_FACTORY_H

#include <cstdint>
#include <string>

#include "GbSipConfig.h"
#include "RegistrationStore.h"
#include "core/SipMessage.h"

namespace easy_sva {
namespace gb28181 {

class SipRequestFactory {
public:
    static SipMessage catalogQuery(const GbSipConfig &config,
                                   const RegisteredDevice &device,
                                   uint64_t serialNumber,
                                   uint64_t cseq,
                                   const std::string &token);

    static SipMessage liveInvite(const GbSipConfig &config,
                                 const RegisteredDevice &device,
                                 const std::string &channelId,
                                 const std::string &sdp,
                                 const std::string &ssrc,
                                 uint64_t cseq,
                                 const std::string &token);

    static SipMessage inviteAck(const GbSipConfig &config,
                                const SipMessage &invite,
                                const SipMessage &finalResponse,
                                const std::string &token);

    static SipMessage dialogBye(const GbSipConfig &config,
                                const SipMessage &invite,
                                const SipMessage &acceptedResponse,
                                uint64_t cseq,
                                const std::string &token);
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_SIP_REQUEST_FACTORY_H
