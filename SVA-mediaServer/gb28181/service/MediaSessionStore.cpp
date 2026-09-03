#include "MediaSessionStore.h"

#include "GbSdp.h"

namespace easy_sva {
namespace gb28181 {

GbMediaSession::GbMediaSession()
    : rtpPort(0), tcp(false), state(GbMediaPreparing), createdAt(0), updatedAt(0) {}

void MediaSessionStore::setError(std::string *error, const std::string &value) {
    if (error) {
        *error = value;
    }
}

bool MediaSessionStore::isTerminal(GbMediaSessionState state) {
    return state == GbMediaStopped || state == GbMediaFailed;
}

const char *MediaSessionStore::stateName(GbMediaSessionState state) {
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

bool MediaSessionStore::canTransition(GbMediaSessionState current,
                                      GbMediaSessionState next) {
    switch (current) {
    case GbMediaPreparing:
        return next == GbMediaInviting || next == GbMediaFailed;
    case GbMediaInviting:
        return next == GbMediaStreaming || next == GbMediaStopping || next == GbMediaFailed;
    case GbMediaStreaming:
        return next == GbMediaStopping || next == GbMediaFailed;
    case GbMediaStopping:
        return next == GbMediaStopped || next == GbMediaFailed;
    case GbMediaStopped:
    case GbMediaFailed:
        return false;
    }
    return false;
}

bool MediaSessionStore::create(const GbMediaSession &session,
                               std::string *error) {
    if (error) {
        error->clear();
    }
    if (session.sessionId.empty() || session.deviceId.empty() ||
        session.channelId.empty() || session.streamId.empty() || session.callId.empty()) {
        setError(error, "GB28181 media session identifiers cannot be empty");
        return false;
    }
    if (session.rtpPort == 0) {
        setError(error, "GB28181 media session RTP port must be positive");
        return false;
    }
    uint32_t numericSsrc = 0;
    if (!GbSdp::parseSsrc(session.ssrc, numericSsrc, error)) {
        return false;
    }
    if (session.state != GbMediaPreparing) {
        setError(error, "new GB28181 media session must start in preparing state");
        return false;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    if (_sessions.find(session.sessionId) != _sessions.end()) {
        setError(error, "GB28181 media session ID already exists");
        return false;
    }
    if (_callIds.find(session.callId) != _callIds.end()) {
        setError(error, "GB28181 media session Call-ID already exists");
        return false;
    }
    for (std::map<std::string, GbMediaSession>::const_iterator it = _sessions.begin();
         it != _sessions.end(); ++it) {
        if (!isTerminal(it->second.state) &&
            it->second.deviceId == session.deviceId &&
            it->second.channelId == session.channelId) {
            setError(error, "GB28181 channel already has an active media session");
            return false;
        }
    }
    _sessions[session.sessionId] = session;
    _callIds[session.callId] = session.sessionId;
    return true;
}

bool MediaSessionStore::transition(const std::string &sessionId,
                                   GbMediaSessionState expected,
                                   GbMediaSessionState next,
                                   uint64_t now,
                                   const std::string &detail,
                                   std::string *error) {
    if (error) {
        error->clear();
    }
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, GbMediaSession>::iterator found = _sessions.find(sessionId);
    if (found == _sessions.end()) {
        setError(error, "GB28181 media session was not found");
        return false;
    }
    if (found->second.state != expected) {
        setError(error, std::string("GB28181 media session is ") +
            stateName(found->second.state) + ", expected " + stateName(expected));
        return false;
    }
    if (!canTransition(expected, next)) {
        setError(error, std::string("invalid GB28181 media session transition from ") +
            stateName(expected) + " to " + stateName(next));
        return false;
    }
    found->second.state = next;
    found->second.updatedAt = now;
    found->second.error = next == GbMediaFailed ? detail : std::string();
    return true;
}

bool MediaSessionStore::find(const std::string &sessionId,
                             GbMediaSession &session) const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, GbMediaSession>::const_iterator found = _sessions.find(sessionId);
    if (found == _sessions.end()) {
        return false;
    }
    session = found->second;
    return true;
}

bool MediaSessionStore::findByCallId(const std::string &callId,
                                     GbMediaSession &session) const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, std::string>::const_iterator indexed = _callIds.find(callId);
    if (indexed == _callIds.end()) {
        return false;
    }
    std::map<std::string, GbMediaSession>::const_iterator found = _sessions.find(indexed->second);
    if (found == _sessions.end()) {
        return false;
    }
    session = found->second;
    return true;
}

bool MediaSessionStore::eraseTerminal(const std::string &sessionId,
                                      std::string *error) {
    if (error) {
        error->clear();
    }
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, GbMediaSession>::iterator found = _sessions.find(sessionId);
    if (found == _sessions.end()) {
        setError(error, "GB28181 media session was not found");
        return false;
    }
    if (!isTerminal(found->second.state)) {
        setError(error, "active GB28181 media session cannot be erased");
        return false;
    }
    _callIds.erase(found->second.callId);
    _sessions.erase(found);
    return true;
}

std::vector<GbMediaSession> MediaSessionStore::list() const {
    std::vector<GbMediaSession> sessions;
    std::lock_guard<std::mutex> lock(_mutex);
    sessions.reserve(_sessions.size());
    for (std::map<std::string, GbMediaSession>::const_iterator it = _sessions.begin();
         it != _sessions.end(); ++it) {
        sessions.push_back(it->second);
    }
    return sessions;
}

size_t MediaSessionStore::size() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _sessions.size();
}

} // namespace gb28181
} // namespace easy_sva
