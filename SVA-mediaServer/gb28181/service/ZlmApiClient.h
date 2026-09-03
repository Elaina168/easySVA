#ifndef EASY_SVA_GB28181_ZLM_API_CLIENT_H
#define EASY_SVA_GB28181_ZLM_API_CLIENT_H

#include <cstdint>
#include <functional>
#include <string>

#include "GbSipConfig.h"

namespace easy_sva {
namespace gb28181 {

struct ZlmHttpRequest {
    std::string url;
    std::string formBody;
    uint32_t timeoutSeconds;
};

struct ZlmHttpResponse {
    int statusCode;
    std::string body;
    std::string error;

    ZlmHttpResponse();
};

struct ZlmRtpOpenOptions {
    std::string streamId;
    std::string localIp;
    uint16_t requestedPort;
    uint32_t ssrc;
    int tcpMode;
    std::string vhost;
    std::string app;

    ZlmRtpOpenOptions();
};

struct ZlmRtpOpenResult {
    bool ok;
    uint16_t port;
    std::string error;

    ZlmRtpOpenResult();
};

struct ZlmRtpCloseResult {
    bool ok;
    bool hit;
    std::string error;

    ZlmRtpCloseResult();
};

class ZlmApiClient {
public:
    typedef std::function<void(const ZlmHttpResponse &)> HttpCompletion;
    typedef std::function<void(const ZlmHttpRequest &, const HttpCompletion &)> Requester;
    typedef std::function<void(const ZlmRtpOpenResult &)> OpenCompletion;
    typedef std::function<void(const ZlmRtpCloseResult &)> CloseCompletion;

    explicit ZlmApiClient(const GbSipConfig &config,
                          const Requester &requester = Requester());

    void openRtpServer(const ZlmRtpOpenOptions &options,
                       const OpenCompletion &completion) const;
    void closeRtpServer(const std::string &streamId,
                        const CloseCompletion &completion) const;

private:
    static void defaultRequest(const ZlmHttpRequest &request,
                               const HttpCompletion &completion);
    static std::string formEncode(const std::string &value);
    static bool parseApiResult(const ZlmHttpResponse &response,
                               int &code,
                               std::string &message,
                               std::string *error);
    static void completeOpen(const OpenCompletion &completion,
                             const ZlmRtpOpenResult &result);
    static void completeClose(const CloseCompletion &completion,
                              const ZlmRtpCloseResult &result);
    std::string endpoint(const std::string &path) const;

    std::string _baseUrl;
    std::string _secret;
    uint32_t _timeoutSeconds;
    Requester _requester;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_ZLM_API_CLIENT_H
