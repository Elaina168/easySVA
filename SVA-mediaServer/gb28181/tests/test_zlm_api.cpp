#include <cstdlib>
#include <iostream>
#include <string>

#include "service/GbSipConfig.h"
#include "service/ZlmApiClient.h"

using easy_sva::gb28181::GbSipConfig;
using easy_sva::gb28181::ZlmApiClient;
using easy_sva::gb28181::ZlmHttpRequest;
using easy_sva::gb28181::ZlmHttpResponse;
using easy_sva::gb28181::ZlmRtpConnectOptions;
using easy_sva::gb28181::ZlmRtpConnectResult;
using easy_sva::gb28181::ZlmRtpCloseResult;
using easy_sva::gb28181::ZlmRtpOpenOptions;
using easy_sva::gb28181::ZlmRtpOpenResult;

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

GbSipConfig sampleConfig() {
    GbSipConfig config;
    config.zlmApiUrl = "http://127.0.0.1:9992/";
    config.zlmApiSecret = "secret with spaces";
    config.zlmApiTimeoutSeconds = 4;
    return config;
}

ZlmRtpOpenOptions sampleOptions() {
    ZlmRtpOpenOptions options;
    options.streamId = "gb channel/1";
    options.localIp = "0.0.0.0";
    options.requestedPort = 0;
    options.ssrc = 100000001;
    options.tcpMode = 0;
    return options;
}

void testOpenRtpServer() {
    ZlmHttpRequest captured;
    ZlmApiClient client(sampleConfig(),
        [&captured](const ZlmHttpRequest &request,
                    const ZlmApiClient::HttpCompletion &completion) {
            captured = request;
            ZlmHttpResponse response;
            response.statusCode = 200;
            response.body = "{\"code\":0,\"port\":31000}";
            completion(response);
        });
    ZlmRtpOpenResult result;
    client.openRtpServer(sampleOptions(), [&result](const ZlmRtpOpenResult &value) {
        result = value;
    });
    expect(result.ok && result.port == 31000, "openRtpServer returns its allocated port");
    expect(captured.url == "http://127.0.0.1:9992/index/api/openRtpServer",
           "openRtpServer uses the configured ZLM endpoint without duplicate slash");
    expect(captured.url.find("secret") == std::string::npos &&
           captured.formBody.find("secret=secret%20with%20spaces") != std::string::npos,
           "API secret is form-encoded in the POST body, not the URL");
    expect(captured.formBody.find("stream_id=gb%20channel%2F1") != std::string::npos &&
           captured.formBody.find("ssrc=100000001") != std::string::npos,
           "stream ID and SSRC are sent to ZLMediaKit");
    expect(captured.formBody.find("tcp_mode=0") != std::string::npos &&
           captured.formBody.find("local_ip=0.0.0.0") != std::string::npos &&
           captured.timeoutSeconds == 4,
           "RTP transport and API timeout settings are preserved");
}

void testCloseRtpServer() {
    int calls = 0;
    ZlmApiClient client(sampleConfig(),
        [&calls](const ZlmHttpRequest &request,
                 const ZlmApiClient::HttpCompletion &completion) {
            ++calls;
            expect(request.url.find("/index/api/closeRtpServer") != std::string::npos,
                   "closeRtpServer uses its dedicated endpoint");
            expect(request.formBody.find("stream_id=gb%2Fstream") != std::string::npos,
                   "closeRtpServer form-encodes the stream ID");
            ZlmHttpResponse response;
            response.statusCode = 200;
            response.body = calls == 1 ?
                "{\"code\":0,\"hit\":1}" : "{\"code\":0,\"hit\":0}";
            completion(response);
        });
    ZlmRtpCloseResult first;
    client.closeRtpServer("gb/stream", [&first](const ZlmRtpCloseResult &value) {
        first = value;
    });
    expect(first.ok && first.hit, "first close reports that an RTP receiver was removed");
    ZlmRtpCloseResult second;
    client.closeRtpServer("gb/stream", [&second](const ZlmRtpCloseResult &value) {
        second = value;
    });
    expect(second.ok && !second.hit, "repeated close is successful and reports no hit");
}

void testConnectRtpServer() {
    ZlmHttpRequest captured;
    ZlmApiClient client(sampleConfig(),
        [&captured](const ZlmHttpRequest &request,
                    const ZlmApiClient::HttpCompletion &completion) {
            captured = request;
            ZlmHttpResponse response;
            response.statusCode = 200;
            response.body = "{\"code\":0}";
            completion(response);
        });
    ZlmRtpConnectOptions options;
    options.streamId = "gb channel/1";
    options.destinationHost = "192.0.2.20";
    options.destinationPort = 62000;
    ZlmRtpConnectResult result;
    client.connectRtpServer(options, [&result](const ZlmRtpConnectResult &value) {
        result = value;
    });
    expect(result.ok, "connectRtpServer accepts a successful ZLM response");
    expect(captured.url == "http://127.0.0.1:9992/index/api/connectRtpServer",
           "connectRtpServer uses the configured ZLM endpoint");
    expect(captured.formBody.find("stream_id=gb%20channel%2F1") != std::string::npos &&
           captured.formBody.find("dst_url=192.0.2.20") != std::string::npos &&
           captured.formBody.find("dst_port=62000") != std::string::npos,
           "connectRtpServer sends the device endpoint and stream ID");

    int requests = 0;
    ZlmApiClient validationClient(sampleConfig(),
        [&requests](const ZlmHttpRequest &, const ZlmApiClient::HttpCompletion &) {
            ++requests;
        });
    options.destinationPort = 0;
    validationClient.connectRtpServer(
        options, [&result](const ZlmRtpConnectResult &value) { result = value; });
    expect(!result.ok && result.error.find("port") != std::string::npos && requests == 0,
           "connectRtpServer rejects an invalid device port before network access");
}

void testValidation() {
    GbSipConfig missingSecret = sampleConfig();
    missingSecret.zlmApiSecret.clear();
    int requests = 0;
    ZlmApiClient client(missingSecret,
        [&requests](const ZlmHttpRequest &, const ZlmApiClient::HttpCompletion &) {
            ++requests;
        });
    ZlmRtpOpenResult result;
    client.openRtpServer(sampleOptions(), [&result](const ZlmRtpOpenResult &value) {
        result = value;
    });
    expect(!result.ok && result.error.find("EASY_SVA_ZLM_API_SECRET") != std::string::npos &&
           requests == 0,
           "missing API secret fails before any network request");

    ZlmRtpOpenOptions invalid = sampleOptions();
    invalid.tcpMode = 3;
    GbSipConfig valid = sampleConfig();
    ZlmApiClient modeClient(valid,
        [&requests](const ZlmHttpRequest &, const ZlmApiClient::HttpCompletion &) {
            ++requests;
        });
    modeClient.openRtpServer(invalid, [&result](const ZlmRtpOpenResult &value) {
        result = value;
    });
    expect(!result.ok && result.error.find("TCP mode") != std::string::npos,
           "unknown RTP TCP mode fails before network access");
}

void testApiFailures() {
    const struct FailureCase {
        int status;
        const char *body;
        const char *transportError;
        const char *expected;
    } cases[] = {
        {0, "", "connection refused", "transport failed"},
        {503, "", "", "HTTP 503"},
        {200, "not-json", "", "invalid JSON"},
        {200, "{\"code\":-100,\"msg\":\"secret error\"}", "", "secret error"},
        {200, "{\"code\":0}", "", "no port"},
        {200, "{\"code\":0,\"port\":70000}", "", "invalid port"}
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const FailureCase current = cases[index];
        ZlmApiClient client(sampleConfig(),
            [current](const ZlmHttpRequest &,
                      const ZlmApiClient::HttpCompletion &completion) {
                ZlmHttpResponse response;
                response.statusCode = current.status;
                response.body = current.body;
                response.error = current.transportError;
                completion(response);
            });
        ZlmRtpOpenResult result;
        client.openRtpServer(sampleOptions(), [&result](const ZlmRtpOpenResult &value) {
            result = value;
        });
        expect(!result.ok && result.error.find(current.expected) != std::string::npos,
               std::string("openRtpServer surfaces failure: ") + current.expected);
    }
}

void testCloseMalformedResponse() {
    ZlmApiClient client(sampleConfig(),
        [](const ZlmHttpRequest &, const ZlmApiClient::HttpCompletion &completion) {
            ZlmHttpResponse response;
            response.statusCode = 200;
            response.body = "{\"code\":0}";
            completion(response);
        });
    ZlmRtpCloseResult result;
    client.closeRtpServer("stream", [&result](const ZlmRtpCloseResult &value) {
        result = value;
    });
    expect(!result.ok && result.error.find("no hit") != std::string::npos,
           "closeRtpServer rejects a success response without hit");
}

} // namespace

int main() {
    testOpenRtpServer();
    testCloseRtpServer();
    testConnectRtpServer();
    testValidation();
    testApiFailures();
    testCloseMalformedResponse();

    if (failures != 0) {
        std::cerr << failures << " GB28181 ZLMediaKit API test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "GB28181 ZLMediaKit API tests passed" << std::endl;
    return EXIT_SUCCESS;
}
