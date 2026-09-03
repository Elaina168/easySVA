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
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_SIP_REQUEST_FACTORY_H
