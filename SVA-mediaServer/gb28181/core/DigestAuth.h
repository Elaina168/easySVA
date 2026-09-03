#ifndef EASY_SVA_GB28181_DIGEST_AUTH_H
#define EASY_SVA_GB28181_DIGEST_AUTH_H

#include <map>
#include <string>

#include "SipMessage.h"

namespace easy_sva {
namespace gb28181 {

struct DigestCredentials {
    std::string username;
    std::string realm;
    std::string nonce;
    std::string uri;
    std::string response;
    std::string algorithm;
    std::string qop;
    std::string nonceCount;
    std::string clientNonce;
};

class DigestAuth {
public:
    static bool parseCredentials(const std::string &authorization,
                                 DigestCredentials &credentials,
                                 std::string *error = nullptr);

    static std::string computeResponse(const std::string &method,
                                       const std::string &password,
                                       const DigestCredentials &credentials);

    static bool verifyRequest(const SipMessage &request,
                              const std::string &password,
                              const std::string &expectedRealm,
                              const std::string &expectedNonce,
                              std::string *error = nullptr);

    static std::string buildChallenge(const std::string &realm,
                                      const std::string &nonce,
                                      bool stale = false);
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_DIGEST_AUTH_H
