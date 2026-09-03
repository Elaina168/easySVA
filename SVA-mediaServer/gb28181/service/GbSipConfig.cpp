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

} // namespace

GbSipConfig::GbSipConfig()
    : serverId("34020000002000000001"),
      realm("3402000000"),
      listenIp("0.0.0.0"),
      sipPort(5060),
      enableUdp(true),
      enableTcp(true),
      idleTimeoutSeconds(180),
      maxMessageBytes(1024 * 1024) {}

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
    readString(ini, "sip.listen_ip", parsed.listenIp);

    unsigned long long port = parsed.sipPort;
    unsigned long long idleTimeout = parsed.idleTimeoutSeconds;
    unsigned long long maxMessageBytes = parsed.maxMessageBytes;
    if (!readUnsigned(ini, "sip.port", 1, 65535, port, error) ||
        !readUnsigned(ini, "sip.idle_timeout_seconds", 1, 86400, idleTimeout, error) ||
        !readUnsigned(ini, "sip.max_message_bytes", 1024, 16 * 1024 * 1024,
                      maxMessageBytes, error) ||
        !readBoolean(ini, "sip.udp", parsed.enableUdp, error) ||
        !readBoolean(ini, "sip.tcp", parsed.enableTcp, error)) {
        return false;
    }

    parsed.sipPort = static_cast<uint16_t>(port);
    parsed.idleTimeoutSeconds = static_cast<uint32_t>(idleTimeout);
    parsed.maxMessageBytes = static_cast<size_t>(maxMessageBytes);
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
    return parse(content.str(), config, error);
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
    return true;
}

} // namespace gb28181
} // namespace easy_sva
