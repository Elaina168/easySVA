#include "GbControlApi.h"

#include <algorithm>
#include <stdexcept>

#include "Common/config.h"
#include "Http/HttpSession.h"
#include "Network/TcpServer.h"
#include "Util/NoticeCenter.h"
#include "json/json.h"

namespace easy_sva {
namespace gb28181 {
namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return ch;
    });
    return value;
}

bool constantTimeEqual(const std::string &left, const std::string &right) {
    const size_t compared = left.size() < right.size() ? left.size() : right.size();
    size_t difference = left.size() ^ right.size();
    for (size_t index = 0; index < compared; ++index) {
        difference |= static_cast<unsigned char>(left[index]) ^
            static_cast<unsigned char>(right[index]);
    }
    return difference == 0;
}

const char *mediaStateName(GbMediaSessionState state) {
    switch (state) {
    case GbMediaPreparing:
        return "preparing";
    case GbMediaInviting:
        return "inviting";
    case GbMediaStreaming:
        return "streaming";
    case GbMediaStopping:
        return "stopping";
    case GbMediaStopped:
        return "stopped";
    case GbMediaFailed:
        return "failed";
    }
    return "unknown";
}

std::string jsonText(const Json::Value &value) {
    Json::FastWriter writer;
    return writer.write(value);
}

GbControlHttpResult jsonResult(int statusCode,
                               int code,
                               const std::string &message,
                               const Json::Value &data = Json::Value()) {
    Json::Value root;
    root["code"] = code;
    root["msg"] = message;
    if (!data.isNull()) {
        root["data"] = data;
    }
    GbControlHttpResult result;
    result.statusCode = statusCode;
    result.body = jsonText(root);
    return result;
}

Json::Value deviceJson(const RegisteredDevice &device) {
    Json::Value value;
    value["device_id"] = device.deviceId;
    value["transport"] = device.transport;
    value["peer_ip"] = device.peerIp;
    value["peer_port"] = device.peerPort;
    value["user_agent"] = device.userAgent;
    value["registered_at"] = Json::UInt64(device.registeredAt);
    value["last_register_at"] = Json::UInt64(device.lastRegisterAt);
    value["expires_at"] = Json::UInt64(device.expiresAt);
    value["last_heartbeat_at"] = Json::UInt64(device.lastHeartbeatAt);
    value["online"] = device.online;
    return value;
}

Json::Value channelJson(const DeviceChannel &channel) {
    Json::Value value;
    value["channel_id"] = channel.deviceId;
    value["name"] = channel.name;
    value["manufacturer"] = channel.manufacturer;
    value["model"] = channel.model;
    value["owner"] = channel.owner;
    value["civil_code"] = channel.civilCode;
    value["address"] = channel.address;
    value["parental"] = channel.parental;
    value["parent_id"] = channel.parentId;
    value["safety_way"] = channel.safetyWay;
    value["register_way"] = channel.registerWay;
    value["secrecy"] = channel.secrecy;
    value["status"] = channel.status;
    value["longitude"] = channel.longitude;
    value["latitude"] = channel.latitude;
    value["last_catalog_at"] = Json::UInt64(channel.lastCatalogAt);
    return value;
}

Json::Value sessionJson(const GbMediaSession &session) {
    Json::Value value;
    value["session_id"] = session.sessionId;
    value["device_id"] = session.deviceId;
    value["channel_id"] = session.channelId;
    value["stream_id"] = session.streamId;
    value["call_id"] = session.callId;
    value["ssrc"] = session.ssrc;
    value["rtp_port"] = session.rtpPort;
    value["transport"] = session.tcp ? "TCP" : "UDP";
    value["state"] = mediaStateName(session.state);
    value["created_at"] = Json::UInt64(session.createdAt);
    value["updated_at"] = Json::UInt64(session.updatedAt);
    value["error"] = session.error;
    return value;
}

} // namespace

GbControlHttpResult::GbControlHttpResult() : statusCode(200) {}

GbControlApi::GbControlApi(const GbSipConfig &config,
                           const RegistrationStore::Ptr &registrations,
                           const DeviceCatalogStore::Ptr &catalogs,
                           const GbLiveService::Ptr &live)
    : _config(config),
      _registrations(registrations ? registrations :
          RegistrationStore::Ptr(new RegistrationStore())),
      _catalogs(catalogs ? catalogs :
          DeviceCatalogStore::Ptr(new DeviceCatalogStore())),
      _live(live),
      _running(false) {}

GbControlApi::~GbControlApi() {
    stop();
}

void GbControlApi::start() {
    if (!_config.apiEnabled || _running) {
        return;
    }
    std::weak_ptr<GbControlApi> weakSelf = shared_from_this();
    toolkit::NoticeCenter::Instance().addListener(
        this, mediakit::Broadcast::kBroadcastHttpRequest,
        [weakSelf](const mediakit::Parser &parser,
                   const mediakit::HttpSession::HttpResponseInvoker &invoker,
                   bool &consumed,
                   toolkit::SockInfo &sender) {
            (void)sender;
            const Ptr self = weakSelf.lock();
            if (!self || parser.url().compare(0, 13, "/gb28181/api/") != 0) {
                return;
            }
            consumed = true;
            Parameters parameters;
            const mediakit::StrCaseMap &urlArgs = parser.getUrlArgs();
            for (mediakit::StrCaseMap::const_iterator it = urlArgs.begin();
                 it != urlArgs.end(); ++it) {
                parameters[it->first] = it->second;
            }
            const GbControlHttpResult result = self->dispatch(
                parser.method(), parser.url(), parser["Authorization"], parameters);
            mediakit::HttpSession::KeyValue headers;
            headers["Content-Type"] = "application/json; charset=utf-8";
            headers["Cache-Control"] = "no-store";
            if (result.statusCode == 401) {
                headers["WWW-Authenticate"] = "Bearer";
            } else if (result.statusCode == 405) {
                headers["Allow"] = "GET";
            }
            invoker(result.statusCode, headers, result.body);
        });

    try {
        _server.reset(new toolkit::TcpServer());
        _server->start<mediakit::HttpSession>(
            _config.apiPort, _config.apiListenIp);
        _running = true;
    } catch (...) {
        toolkit::NoticeCenter::Instance().delListener(
            this, mediakit::Broadcast::kBroadcastHttpRequest);
        _server.reset();
        throw;
    }
}

void GbControlApi::stop() {
    if (!_running && !_server) {
        return;
    }
    toolkit::NoticeCenter::Instance().delListener(
        this, mediakit::Broadcast::kBroadcastHttpRequest);
    _server.reset();
    _running = false;
}

bool GbControlApi::running() const {
    return _running;
}

bool GbControlApi::authorized(const std::string &authorization) const {
    if (_config.apiSecret.empty()) {
        return true;
    }
    const std::string prefix = "bearer ";
    if (authorization.size() <= prefix.size() ||
        lowerAscii(authorization.substr(0, prefix.size())) != prefix) {
        return false;
    }
    return constantTimeEqual(
        authorization.substr(prefix.size()), _config.apiSecret);
}

GbControlHttpResult GbControlApi::dispatch(
        const std::string &method,
        const std::string &path,
        const std::string &authorization,
        const Parameters &parameters) const {
    if (method != "GET") {
        return jsonResult(405, 405, "method not allowed");
    }
    if (path == "/gb28181/api/health") {
        Json::Value data;
        data["service"] = "easySVA-GB28181";
        data["status"] = "ok";
        return jsonResult(200, 0, "success", data);
    }
    if (!authorized(authorization)) {
        return jsonResult(401, 401, "unauthorized");
    }
    if (path == "/gb28181/api/devices") {
        Json::Value data(Json::arrayValue);
        const std::vector<RegisteredDevice> devices = _registrations->list();
        for (std::vector<RegisteredDevice>::const_iterator it = devices.begin();
             it != devices.end(); ++it) {
            data.append(deviceJson(*it));
        }
        return jsonResult(200, 0, "success", data);
    }
    if (path == "/gb28181/api/catalog") {
        Parameters::const_iterator deviceId = parameters.find("device_id");
        if (deviceId == parameters.end() || deviceId->second.empty()) {
            return jsonResult(400, 400, "device_id is required");
        }
        Json::Value data(Json::arrayValue);
        const std::vector<DeviceChannel> channels =
            _catalogs->list(deviceId->second);
        for (std::vector<DeviceChannel>::const_iterator it = channels.begin();
             it != channels.end(); ++it) {
            data.append(channelJson(*it));
        }
        return jsonResult(200, 0, "success", data);
    }
    if (path == "/gb28181/api/sessions") {
        if (!_live) {
            return jsonResult(503, 503, "live service is unavailable");
        }
        Json::Value data(Json::arrayValue);
        const std::vector<GbMediaSession> sessions = _live->listSessions();
        for (std::vector<GbMediaSession>::const_iterator it = sessions.begin();
             it != sessions.end(); ++it) {
            data.append(sessionJson(*it));
        }
        return jsonResult(200, 0, "success", data);
    }
    return jsonResult(404, 404, "not found");
}

} // namespace gb28181
} // namespace easy_sva
