#ifndef EASY_SVA_GB28181_SIP_REQUEST_PROCESSOR_H
#define EASY_SVA_GB28181_SIP_REQUEST_PROCESSOR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/SipMessage.h"

namespace easy_sva {
namespace gb28181 {

struct SipPeer {
    typedef std::function<bool(const std::string &)> Sender;

    std::string transport;
    std::string ip;
    uint16_t port;
    Sender sender;

    SipPeer();
};

class SipRequestProcessor {
public:
    typedef std::shared_ptr<SipRequestProcessor> Ptr;
    virtual ~SipRequestProcessor() {}

    virtual bool process(const SipMessage &message,
                         const SipPeer &peer,
                         SipMessage &response) = 0;
};

class BasicSipRequestProcessor : public SipRequestProcessor {
public:
    explicit BasicSipRequestProcessor(const std::string &serverTag);

    bool process(const SipMessage &message,
                 const SipPeer &peer,
                 SipMessage &response) override;

    static const char *allowedMethods();

private:
    std::string _server_tag;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_SIP_REQUEST_PROCESSOR_H
