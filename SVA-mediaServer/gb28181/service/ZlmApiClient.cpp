#include "ZlmApiClient.h"

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <sstream>

#include "Http/HttpRequester.h"
#include "json/json.h"

namespace easy_sva {
namespace gb28181 {
namespace {

std::string jsonMessage(const Json::Value &root) {
    if (root.isMember("msg") && root["msg"].isString()) {
        return root["msg"].asString();
    }
    return std::string();
}

} // namespace

ZlmHttpResponse::ZlmHttpResponse() : statusCode(0) {}

ZlmRtpOpenOptions::ZlmRtpOpenOptions()
    : localIp("0.0.0.0"), requestedPort(0), ssrc(0), tcpMode(0),
      vhost("__defaultVhost__"), app("rtp") {}

ZlmRtpOpenResult::ZlmRtpOpenResult() : ok(false), port(0) {}

ZlmRtpConnectOptions::ZlmRtpConnectOptions()
    : destinationPort(0), vhost("__defaultVhost__"), app("rtp") {}

ZlmRtpConnectResult::ZlmRtpConnectResult() : ok(false) {}

ZlmRtpCloseResult::ZlmRtpCloseResult() : ok(false), hit(false) {}

ZlmApiClient::ZlmApiClient(const GbSipConfig &config,
                           const Requester &requester)
    : _baseUrl(config.zlmApiUrl),
      _secret(config.zlmApiSecret),
      _timeoutSeconds(config.zlmApiTimeoutSeconds),
      _requester(requester ? requester : Requester(defaultRequest)) {
    while (!_baseUrl.empty() && _baseUrl[_baseUrl.size() - 1] == '/') {
        _baseUrl.erase(_baseUrl.size() - 1);
    }
}

std::string ZlmApiClient::endpoint(const std::string &path) const {
    return _baseUrl + path;
}

std::string ZlmApiClient::formEncode(const std::string &value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char ch = static_cast<unsigned char>(*it);
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded << static_cast<char>(ch);
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return encoded.str();
}

void ZlmApiClient::defaultRequest(const ZlmHttpRequest &request,
                                  const HttpCompletion &completion) {
    std::shared_ptr<mediakit::HttpRequester> requester(new mediakit::HttpRequester());
    requester->setMethod("POST");
    requester->addHeader("Content-Type", "application/x-www-form-urlencoded");
    requester->setBody(request.formBody);
    requester->startRequester(
        request.url,
        [requester, completion](const toolkit::SockException &exception,
                                const mediakit::Parser &response) {
            ZlmHttpResponse result;
            if (exception) {
                result.error = exception.what();
            } else {
                result.statusCode = std::atoi(response.status().c_str());
                result.body = response.content();
            }
            if (completion) {
                completion(result);
            }
        },
        static_cast<float>(request.timeoutSeconds));
}

bool ZlmApiClient::parseApiResult(const ZlmHttpResponse &response,
                                  int &code,
                                  std::string &message,
                                  std::string *error) {
    if (!response.error.empty()) {
        if (error) {
            *error = "ZLMediaKit API transport failed: " + response.error;
        }
        return false;
    }
    if (response.statusCode != 200) {
        if (error) {
            *error = "ZLMediaKit API returned HTTP " + std::to_string(response.statusCode);
        }
        return false;
    }
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(response.body, root, false) || !root.isObject() ||
        !root.isMember("code") || !root["code"].isNumeric()) {
        if (error) {
            *error = "ZLMediaKit API returned invalid JSON";
        }
        return false;
    }
    code = root["code"].asInt();
    message = jsonMessage(root);
    if (code != 0) {
        if (error) {
            *error = "ZLMediaKit API failed with code " + std::to_string(code);
            if (!message.empty()) {
                *error += ": " + message;
            }
        }
        return false;
    }
    return true;
}

void ZlmApiClient::completeOpen(const OpenCompletion &completion,
                                const ZlmRtpOpenResult &result) {
    if (completion) {
        completion(result);
    }
}

void ZlmApiClient::completeConnect(const ConnectCompletion &completion,
                                   const ZlmRtpConnectResult &result) {
    if (completion) {
        completion(result);
    }
}

void ZlmApiClient::completeClose(const CloseCompletion &completion,
                                 const ZlmRtpCloseResult &result) {
    if (completion) {
        completion(result);
    }
}

void ZlmApiClient::openRtpServer(const ZlmRtpOpenOptions &options,
                                 const OpenCompletion &completion) const {
    ZlmRtpOpenResult invalid;
    if (_baseUrl.empty()) {
        invalid.error = "ZLMediaKit API URL is empty";
        completeOpen(completion, invalid);
        return;
    }
    if (_secret.empty()) {
        invalid.error = "EASY_SVA_ZLM_API_SECRET is not configured";
        completeOpen(completion, invalid);
        return;
    }
    if (options.streamId.empty()) {
        invalid.error = "ZLMediaKit RTP stream ID is empty";
        completeOpen(completion, invalid);
        return;
    }
    if (options.localIp.empty()) {
        invalid.error = "ZLMediaKit RTP listen address is empty";
        completeOpen(completion, invalid);
        return;
    }
    if (options.ssrc == 0) {
        invalid.error = "ZLMediaKit RTP SSRC must be positive";
        completeOpen(completion, invalid);
        return;
    }
    if (options.tcpMode < 0 || options.tcpMode > 2) {
        invalid.error = "ZLMediaKit RTP TCP mode must be 0, 1, or 2";
        completeOpen(completion, invalid);
        return;
    }

    ZlmHttpRequest request;
    request.url = endpoint("/index/api/openRtpServer");
    request.timeoutSeconds = _timeoutSeconds;
    request.formBody =
        "secret=" + formEncode(_secret) +
        "&port=" + std::to_string(options.requestedPort) +
        "&stream_id=" + formEncode(options.streamId) +
        "&tcp_mode=" + std::to_string(options.tcpMode) +
        "&ssrc=" + std::to_string(options.ssrc) +
        "&only_track=0&re_use_port=0" +
        "&local_ip=" + formEncode(options.localIp) +
        "&vhost=" + formEncode(options.vhost) +
        "&app=" + formEncode(options.app);

    _requester(request, [completion](const ZlmHttpResponse &response) {
        ZlmRtpOpenResult result;
        int code = 0;
        std::string message;
        if (!parseApiResult(response, code, message, &result.error)) {
            completeOpen(completion, result);
            return;
        }
        Json::Value root;
        Json::Reader reader;
        reader.parse(response.body, root, false);
        if (!root.isMember("port") || !root["port"].isNumeric()) {
            result.error = "ZLMediaKit openRtpServer response has no port";
            completeOpen(completion, result);
            return;
        }
        const int port = root["port"].asInt();
        if (port <= 0 || port > 65535) {
            result.error = "ZLMediaKit openRtpServer returned an invalid port";
            completeOpen(completion, result);
            return;
        }
        result.ok = true;
        result.port = static_cast<uint16_t>(port);
        completeOpen(completion, result);
    });
}

void ZlmApiClient::connectRtpServer(const ZlmRtpConnectOptions &options,
                                    const ConnectCompletion &completion) const {
    ZlmRtpConnectResult invalid;
    if (_baseUrl.empty()) {
        invalid.error = "ZLMediaKit API URL is empty";
        completeConnect(completion, invalid);
        return;
    }
    if (_secret.empty()) {
        invalid.error = "EASY_SVA_ZLM_API_SECRET is not configured";
        completeConnect(completion, invalid);
        return;
    }
    if (options.streamId.empty()) {
        invalid.error = "ZLMediaKit RTP stream ID is empty";
        completeConnect(completion, invalid);
        return;
    }
    if (options.destinationHost.empty()) {
        invalid.error = "GB28181 TCP active destination address is empty";
        completeConnect(completion, invalid);
        return;
    }
    if (options.destinationPort == 0) {
        invalid.error = "GB28181 TCP active destination port must be positive";
        completeConnect(completion, invalid);
        return;
    }

    ZlmHttpRequest request;
    request.url = endpoint("/index/api/connectRtpServer");
    request.timeoutSeconds = _timeoutSeconds;
    request.formBody =
        "secret=" + formEncode(_secret) +
        "&stream_id=" + formEncode(options.streamId) +
        "&dst_url=" + formEncode(options.destinationHost) +
        "&dst_port=" + std::to_string(options.destinationPort) +
        "&vhost=" + formEncode(options.vhost) +
        "&app=" + formEncode(options.app);

    _requester(request, [completion](const ZlmHttpResponse &response) {
        ZlmRtpConnectResult result;
        int code = 0;
        std::string message;
        if (!parseApiResult(response, code, message, &result.error)) {
            completeConnect(completion, result);
            return;
        }
        result.ok = true;
        completeConnect(completion, result);
    });
}

void ZlmApiClient::closeRtpServer(const std::string &streamId,
                                  const CloseCompletion &completion) const {
    ZlmRtpCloseResult invalid;
    if (_baseUrl.empty()) {
        invalid.error = "ZLMediaKit API URL is empty";
        completeClose(completion, invalid);
        return;
    }
    if (_secret.empty()) {
        invalid.error = "EASY_SVA_ZLM_API_SECRET is not configured";
        completeClose(completion, invalid);
        return;
    }
    if (streamId.empty()) {
        invalid.error = "ZLMediaKit RTP stream ID is empty";
        completeClose(completion, invalid);
        return;
    }

    ZlmHttpRequest request;
    request.url = endpoint("/index/api/closeRtpServer");
    request.timeoutSeconds = _timeoutSeconds;
    request.formBody = "secret=" + formEncode(_secret) +
        "&stream_id=" + formEncode(streamId);
    _requester(request, [completion](const ZlmHttpResponse &response) {
        ZlmRtpCloseResult result;
        int code = 0;
        std::string message;
        if (!parseApiResult(response, code, message, &result.error)) {
            completeClose(completion, result);
            return;
        }
        Json::Value root;
        Json::Reader reader;
        reader.parse(response.body, root, false);
        if (!root.isMember("hit") || !root["hit"].isNumeric()) {
            result.error = "ZLMediaKit closeRtpServer response has no hit field";
            completeClose(completion, result);
            return;
        }
        result.ok = true;
        result.hit = root["hit"].asInt() != 0;
        completeClose(completion, result);
    });
}

} // namespace gb28181
} // namespace easy_sva
