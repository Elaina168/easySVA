#include "GbSipConfig.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

#include "Util/mini.h"

namespace easy_sva {
namespace gb28181 {
namespace {

void setError(std::string *error, const std::string &value) {
    if (error) {
        *error = value;
    }
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return ch;
    });
    return value;
}

bool isDigits(const std::string &value) {
    if (value.empty()) {
        return false;
    }
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (*it < '0' || *it > '9') {
            return false;
        }
    }
    return true;
}

bool readUnsigned(const toolkit::mINI &ini,
                  const std::string &key,
                  unsigned long long minimum,
                  unsigned long long maximum,
                  unsigned long long &value,
                  std::string *error) {
    toolkit::mINI::const_iterator it = ini.find(key);
    if (it == ini.end()) {
        return true;
    }
    errno = 0;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(it->second.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != '\0' || parsed < minimum || parsed > maximum) {
        setError(error, "invalid value for " + key);
        return false;
    }
    value = parsed;
    return true;
}

bool readBoolean(const toolkit::mINI &ini,
                 const std::string &key,
                 bool &value,
                 std::string *error) {
    toolkit::mINI::const_iterator it = ini.find(key);
    if (it == ini.end()) {
        return true;
    }
    const std::string normalized = lowerAscii(it->second);
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        value = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        value = false;
        return true;
    }
    setError(error, "invalid value for " + key);
    return false;
}

void readString(const toolkit::mINI &ini, const std::string &key, std::string &value) {
    toolkit::mINI::const_iterator it = ini.find(key);
    if (it != ini.end()) {
        value = it->second;
    }
}

bool isLoopbackAddress(const std::string &address) {
    const std::string normalized = lowerAscii(address);
    return normalized == "localhost" || normalized == "::1" ||
        normalized.compare(0, 4, "127.") == 0;
}

} // namespace

GbSipConfig::GbSipConfig()
    : serverId("34020000002000000001"),
      realm("3402000000"),
      advertisedIp("127.0.0.1"),
      listenIp("0.0.0.0"),
      sipPort(5060),
      enableUdp(true),
      enableTcp(true),
      idleTimeoutSeconds(180),
      authRequired(true),
      devicePassword("12345678"),
      nonceTtlSeconds(300),
      defaultRegisterExpires(3600),
      minRegisterExpires(60),
      maxRegisterExpires(86400),
      heartbeatTimeoutSeconds(90),
      transactionTimeoutSeconds(5),
      maxMessageBytes(1024 * 1024),
      apiEnabled(true),
      apiListenIp("127.0.0.1"),
      apiPort(18080),
      zlmApiUrl("http://127.0.0.1:9992"),
      zlmApiTimeoutSeconds(5),
      rtpListenIp("0.0.0.0"),
      rtpPort(0),
      rtpTcpMode(0) {}

bool GbSipConfig::parse(const std::string &text,
                        GbSipConfig &config,
                        std::string *error) {
    if (error) {
        error->clear();
    }
    toolkit::mINI ini;
    ini.parse(text);

    GbSipConfig parsed;
    readString(ini, "sip.server_id", parsed.serverId);
    readString(ini, "sip.realm", parsed.realm);
    readString(ini, "sip.advertised_ip", parsed.advertisedIp);
    readString(ini, "sip.listen_ip", parsed.listenIp);
    readString(ini, "registration.device_password", parsed.devicePassword);
    readString(ini, "api.listen_ip", parsed.apiListenIp);
    readString(ini, "api.secret", parsed.apiSecret);
    readString(ini, "media.zlm_api_url", parsed.zlmApiUrl);
    readString(ini, "media.zlm_api_secret", parsed.zlmApiSecret);
    readString(ini, "media.rtp_advertised_ip", parsed.rtpAdvertisedIp);
    readString(ini, "media.rtp_listen_ip", parsed.rtpListenIp);
    if (parsed.rtpAdvertisedIp.empty()) {
        parsed.rtpAdvertisedIp = parsed.advertisedIp;
    }

    unsigned long long port = parsed.sipPort;
    unsigned long long idleTimeout = parsed.idleTimeoutSeconds;
    unsigned long long maxMessageBytes = parsed.maxMessageBytes;
    unsigned long long nonceTtl = parsed.nonceTtlSeconds;
    unsigned long long defaultExpires = parsed.defaultRegisterExpires;
    unsigned long long minExpires = parsed.minRegisterExpires;
    unsigned long long maxExpires = parsed.maxRegisterExpires;
    unsigned long long heartbeatTimeout = parsed.heartbeatTimeoutSeconds;
    unsigned long long transactionTimeout = parsed.transactionTimeoutSeconds;
    unsigned long long apiPort = parsed.apiPort;
    unsigned long long zlmApiTimeout = parsed.zlmApiTimeoutSeconds;
    unsigned long long rtpPort = parsed.rtpPort;
    unsigned long long rtpTcpMode = parsed.rtpTcpMode;
    if (!readUnsigned(ini, "sip.port", 1, 65535, port, error) ||
        !readUnsigned(ini, "sip.idle_timeout_seconds", 1, 86400, idleTimeout, error) ||
        !readUnsigned(ini, "sip.max_message_bytes", 1024, 16 * 1024 * 1024,
                      maxMessageBytes, error) ||
        !readUnsigned(ini, "registration.nonce_ttl_seconds", 10, 3600, nonceTtl, error) ||
        !readUnsigned(ini, "registration.default_expires_seconds", 1, 604800, defaultExpires, error) ||
        !readUnsigned(ini, "registration.min_expires_seconds", 1, 86400, minExpires, error) ||
        !readUnsigned(ini, "registration.max_expires_seconds", 1, 604800, maxExpires, error) ||
        !readUnsigned(ini, "device.heartbeat_timeout_seconds", 3, 86400,
                      heartbeatTimeout, error) ||
        !readUnsigned(ini, "sip.transaction_timeout_seconds", 1, 60,
                      transactionTimeout, error) ||
        !readUnsigned(ini, "api.port", 1, 65535, apiPort, error) ||
        !readBoolean(ini, "api.enabled", parsed.apiEnabled, error) ||
        !readUnsigned(ini, "media.zlm_api_timeout_seconds", 1, 60,
                      zlmApiTimeout, error) ||
        !readUnsigned(ini, "media.rtp_port", 0, 65535,
                      rtpPort, error) ||
        !readUnsigned(ini, "media.rtp_tcp_mode", 0, 2,
                      rtpTcpMode, error) ||
        !readBoolean(ini, "sip.udp", parsed.enableUdp, error) ||
        !readBoolean(ini, "sip.tcp", parsed.enableTcp, error) ||
        !readBoolean(ini, "registration.auth_required", parsed.authRequired, error)) {
        return false;
    }

    parsed.sipPort = static_cast<uint16_t>(port);
    parsed.idleTimeoutSeconds = static_cast<uint32_t>(idleTimeout);
    parsed.maxMessageBytes = static_cast<size_t>(maxMessageBytes);
    parsed.nonceTtlSeconds = static_cast<uint32_t>(nonceTtl);
    parsed.defaultRegisterExpires = static_cast<uint32_t>(defaultExpires);
    parsed.minRegisterExpires = static_cast<uint32_t>(minExpires);
    parsed.maxRegisterExpires = static_cast<uint32_t>(maxExpires);
    parsed.heartbeatTimeoutSeconds = static_cast<uint32_t>(heartbeatTimeout);
    parsed.transactionTimeoutSeconds = static_cast<uint32_t>(transactionTimeout);
    parsed.apiPort = static_cast<uint16_t>(apiPort);
    parsed.zlmApiTimeoutSeconds = static_cast<uint32_t>(zlmApiTimeout);
    parsed.rtpPort = static_cast<uint16_t>(rtpPort);
    parsed.rtpTcpMode = static_cast<int>(rtpTcpMode);
    if (!parsed.validate(error)) {
        return false;
    }
    config = parsed;
    return true;
}

bool GbSipConfig::load(const std::string &path,
                       GbSipConfig &config,
                       std::string *error) {
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input.good()) {
        setError(error, "cannot open GB28181 config: " + path);
        return false;
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (!input.good() && !input.eof()) {
        setError(error, "cannot read GB28181 config: " + path);
        return false;
    }
    if (!parse(content.str(), config, error)) {
        return false;
    }
    const char *environmentSecret = std::getenv("EASY_SVA_ZLM_API_SECRET");
    if (environmentSecret && *environmentSecret) {
        config.zlmApiSecret = environmentSecret;
    }
    const char *apiEnvironmentSecret = std::getenv("EASY_SVA_GB_API_SECRET");
    if (apiEnvironmentSecret && *apiEnvironmentSecret) {
        config.apiSecret = apiEnvironmentSecret;
    }
    return true;
}

bool GbSipConfig::validate(std::string *error) const {
    if (serverId.size() != 20 || !isDigits(serverId)) {
        setError(error, "sip.server_id must be a 20-digit GB28181 platform ID");
        return false;
    }
    if (realm.size() != 10 || !isDigits(realm)) {
        setError(error, "sip.realm must be a 10-digit GB28181 domain code");
        return false;
    }
    if (advertisedIp.empty() || advertisedIp == "0.0.0.0" || advertisedIp == "::") {
        setError(error, "sip.advertised_ip must be a reachable unicast address");
        return false;
    }
    if (listenIp.empty()) {
        setError(error, "sip.listen_ip cannot be empty");
        return false;
    }
    if (sipPort == 0) {
        setError(error, "sip.port must be between 1 and 65535");
        return false;
    }
    if (!enableUdp && !enableTcp) {
        setError(error, "at least one of sip.udp and sip.tcp must be enabled");
        return false;
    }
    if (idleTimeoutSeconds == 0) {
        setError(error, "sip.idle_timeout_seconds must be positive");
        return false;
    }
    if (maxMessageBytes < 1024 || maxMessageBytes > 16 * 1024 * 1024) {
        setError(error, "sip.max_message_bytes must be between 1024 and 16777216");
        return false;
    }
    if (authRequired && devicePassword.empty()) {
        setError(error, "registration.device_password is required when authentication is enabled");
        return false;
    }
    if (nonceTtlSeconds < 10 || nonceTtlSeconds > 3600) {
        setError(error, "registration.nonce_ttl_seconds must be between 10 and 3600");
        return false;
    }
    if (minRegisterExpires == 0 || maxRegisterExpires < minRegisterExpires) {
        setError(error, "registration expiry bounds are invalid");
        return false;
    }
    if (defaultRegisterExpires < minRegisterExpires ||
        defaultRegisterExpires > maxRegisterExpires) {
        setError(error, "registration.default_expires_seconds must be within the expiry bounds");
        return false;
    }
    if (heartbeatTimeoutSeconds < 3 || heartbeatTimeoutSeconds > 86400) {
        setError(error, "device.heartbeat_timeout_seconds must be between 3 and 86400");
        return false;
    }
    if (transactionTimeoutSeconds == 0 || transactionTimeoutSeconds > 60) {
        setError(error, "sip.transaction_timeout_seconds must be between 1 and 60");
        return false;
    }
    if (apiEnabled && apiListenIp.empty()) {
        setError(error, "api.listen_ip cannot be empty when the API is enabled");
        return false;
    }
    if (apiEnabled && apiPort == 0) {
        setError(error, "api.port must be between 1 and 65535 when the API is enabled");
        return false;
    }
    const char *apiEnvironmentSecret = std::getenv("EASY_SVA_GB_API_SECRET");
    const bool hasApiSecret = !apiSecret.empty() ||
        (apiEnvironmentSecret && *apiEnvironmentSecret);
    if (apiEnabled && !isLoopbackAddress(apiListenIp) && !hasApiSecret) {
        setError(error,
            "api.secret or EASY_SVA_GB_API_SECRET is required for a non-loopback API listener");
        return false;
    }
    if (zlmApiUrl.compare(0, 7, "http://") != 0 &&
        zlmApiUrl.compare(0, 8, "https://") != 0) {
        setError(error, "media.zlm_api_url must use http:// or https://");
        return false;
    }
    if (zlmApiTimeoutSeconds == 0 || zlmApiTimeoutSeconds > 60) {
        setError(error, "media.zlm_api_timeout_seconds must be between 1 and 60");
        return false;
    }
    if (rtpAdvertisedIp.empty()) {
        setError(error, "media.rtp_advertised_ip cannot be empty after fallback");
        return false;
    }
    if (rtpListenIp.empty()) {
        setError(error, "media.rtp_listen_ip cannot be empty");
        return false;
    }
    if (rtpTcpMode < 0 || rtpTcpMode > 2) {
        setError(error, "media.rtp_tcp_mode must be 0, 1, or 2");
        return false;
    }
    return true;
}

} // namespace gb28181
} // namespace easy_sva
