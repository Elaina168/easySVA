#ifndef EASY_SVA_GB28181_MEDIA_SESSION_STORE_H
#define EASY_SVA_GB28181_MEDIA_SESSION_STORE_H

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace easy_sva {
namespace gb28181 {

enum GbMediaSessionState {
    GbMediaPreparing,
    GbMediaInviting,
    GbMediaStreaming,
    GbMediaStopping,
    GbMediaStopped,
    GbMediaFailed
};

struct GbMediaSession {
    std::string sessionId;
    std::string deviceId;
    std::string channelId;
    std::string streamId;
    std::string callId;
    std::string ssrc;
    uint16_t rtpPort;
    bool tcp;
    GbMediaSessionState state;
    uint64_t createdAt;
    uint64_t updatedAt;
    std::string error;

    GbMediaSession();
};

class MediaSessionStore {
public:
    typedef std::shared_ptr<MediaSessionStore> Ptr;

    bool create(const GbMediaSession &session,
                std::string *error = nullptr);
    bool transition(const std::string &sessionId,
                    GbMediaSessionState expected,
                    GbMediaSessionState next,
                    uint64_t now,
                    const std::string &detail = std::string(),
                    std::string *error = nullptr);
    bool find(const std::string &sessionId, GbMediaSession &session) const;
    bool findByCallId(const std::string &callId, GbMediaSession &session) const;
    bool eraseTerminal(const std::string &sessionId,
                       std::string *error = nullptr);
    std::vector<GbMediaSession> list() const;
    size_t size() const;

private:
    static bool canTransition(GbMediaSessionState current,
                              GbMediaSessionState next);
    static bool isTerminal(GbMediaSessionState state);
    static const char *stateName(GbMediaSessionState state);
    static void setError(std::string *error, const std::string &value);

    mutable std::mutex _mutex;
    std::map<std::string, GbMediaSession> _sessions;
    std::map<std::string, std::string> _callIds;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_MEDIA_SESSION_STORE_H
