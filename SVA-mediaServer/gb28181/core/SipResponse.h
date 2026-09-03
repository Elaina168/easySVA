#ifndef EASY_SVA_GB28181_SIP_RESPONSE_H
#define EASY_SVA_GB28181_SIP_RESPONSE_H

#include <string>

#include "SipMessage.h"

namespace easy_sva {
namespace gb28181 {

class SipResponse {
public:
    static SipMessage fromRequest(const SipMessage &request,
                                  int statusCode,
                                  const std::string &reasonPhrase,
                                  const std::string &serverTag,
                                  const std::string &body = std::string(),
                                  const std::string &contentType = std::string());

    static SipMessage badRequest();
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_SIP_RESPONSE_H
