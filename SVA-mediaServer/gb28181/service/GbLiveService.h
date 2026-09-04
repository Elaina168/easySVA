#ifndef EASY_SVA_GB28181_GB_LIVE_SERVICE_H
#define EASY_SVA_GB28181_GB_LIVE_SERVICE_H

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "DeviceCatalogStore.h"
#include "GbSipConfig.h"
#include "MediaSessionStore.h"
#include "RegistrationStore.h"
#include "SipClientTransaction.h"
#include "ZlmApiClient.h"

namespace easy_sva {
namespace gb28181 {

class GbLiveService : public std::enable_shared_from_this<GbLiveService> {
public:
    typedef std::shared_ptr<GbLiveService> Ptr;
    typedef std::function<uint64_t()> Clock;

    GbLiveService(const GbSipConfig &config,
                  const RegistrationStore::Ptr &registrations,
                  const DeviceCatalogStore::Ptr &catalogs,
                  const Clock &clock = Clock(),
                  const ZlmApiClient::Requester &zlmRequester = ZlmApiClient::Requester());

    std::string startLive(const std::string &deviceId,
                          const std::string &channelId,
                          std::string *error = nullptr);
    bool stopLive(const std::string &sessionId,
                  std::string *error = nullptr);
    bool handleResponse(const SipMessage &response);
    void sweep();

    bool findSession(const std::string &sessionId,
                     GbMediaSession &session) const;
    std::vector<GbMediaSession> listSessions() const;
    size_t pendingTransactions() const;

private:
    struct MediaDialog {
        SipMessage invite;
        SipMessage acceptedResponse;
        RegisteredDevice::Sender sender;
        uint64_t nextCseq;

        MediaDialog();
    };

    void onRtpOpened(const std::string &sessionId,
                     const std::string &streamId,
                     const RegisteredDevice &device,
                     uint64_t inviteCseq,
                     const ZlmRtpOpenResult &result);
    void finishInvite(const std::string &sessionId,
                      const RegisteredDevice &device,
                      const SipMessage &invite,
                      uint64_t inviteCseq,
                      const SipTransactionResult &result);
    bool beginBye(const std::string &sessionId,
                  const std::string &failureReason,
                  std::string *error = nullptr);
    void finishBye(const std::string &sessionId,
                   const std::string &failureReason,
                   const SipTransactionResult &result);
    void finishAfterRtpClose(const std::string &sessionId,
                             GbMediaSessionState terminalState,
                             const std::string &detail,
                             const ZlmRtpCloseResult &result);
    void failBeforeDialog(const std::string &sessionId,
                          GbMediaSessionState expected,
                          const std::string &detail,
                          bool closeRtp);
    bool validateAcceptedSdp(const GbMediaSession &session,
                             const SipMessage &response,
                             std::string &error) const;
    std::string token(const std::string &prefix, uint64_t sequence) const;
    static std::string ssrcForSequence(uint64_t sequence);

    GbSipConfig _config;
    RegistrationStore::Ptr _registrations;
    DeviceCatalogStore::Ptr _catalogs;
    Clock _clock;
    SipClientTransactionStore::Ptr _transactions;
    MediaSessionStore::Ptr _sessions;
    std::shared_ptr<ZlmApiClient> _zlm;
    std::atomic<uint64_t> _sequence;
    mutable std::mutex _mutex;
    std::map<std::string, MediaDialog> _dialogs;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_GB_LIVE_SERVICE_H
