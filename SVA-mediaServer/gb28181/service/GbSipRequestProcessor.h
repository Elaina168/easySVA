#ifndef EASY_SVA_GB28181_GB_SIP_REQUEST_PROCESSOR_H
#define EASY_SVA_GB28181_GB_SIP_REQUEST_PROCESSOR_H

#include <functional>
#include <memory>
#include <string>

#include "DigestNonceStore.h"
#include "GbSipConfig.h"
#include "RegistrationStore.h"
#include "SipRequestProcessor.h"

namespace easy_sva {
namespace gb28181 {

class GbSipRequestProcessor : public SipRequestProcessor {
public:
    typedef std::function<uint64_t()> Clock;

    explicit GbSipRequestProcessor(const GbSipConfig &config);
    GbSipRequestProcessor(const GbSipConfig &config,
                          const RegistrationStore::Ptr &registrations,
                          const DigestNonceStore::Ptr &nonces,
                          const Clock &clock);

    bool process(const SipMessage &message,
                 const SipPeer &peer,
                 SipMessage &response) override;

    const RegistrationStore::Ptr &registrations() const;
    const DigestNonceStore::Ptr &nonces() const;

private:
    bool processRegister(const SipMessage &request,
                         const SipPeer &peer,
                         SipMessage &response);
    bool challenge(const SipMessage &request,
                   const std::string &deviceId,
                   bool stale,
                   SipMessage &response);

    GbSipConfig _config;
    RegistrationStore::Ptr _registrations;
    DigestNonceStore::Ptr _nonces;
    Clock _clock;
    BasicSipRequestProcessor _fallback;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_GB_SIP_REQUEST_PROCESSOR_H
