#ifndef EASY_SVA_GB28181_GB_PLATFORM_SERVICE_H
#define EASY_SVA_GB28181_GB_PLATFORM_SERVICE_H

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "DeviceCatalogStore.h"
#include "GbSipConfig.h"
#include "RegistrationStore.h"
#include "SipClientTransaction.h"

namespace easy_sva {
namespace gb28181 {

enum PlatformCommandState {
    PlatformCommandPending,
    PlatformCommandSucceeded,
    PlatformCommandFailed,
    PlatformCommandTimedOut
};

struct PlatformCommand {
    std::string commandId;
    std::string type;
    std::string deviceId;
    PlatformCommandState state;
    int sipStatus;
    std::string error;
    uint64_t createdAt;
    uint64_t completedAt;

    PlatformCommand();
};

class GbPlatformService : public std::enable_shared_from_this<GbPlatformService> {
public:
    typedef std::shared_ptr<GbPlatformService> Ptr;
    typedef std::function<uint64_t()> Clock;

    GbPlatformService(const GbSipConfig &config,
                      const RegistrationStore::Ptr &registrations,
                      const DeviceCatalogStore::Ptr &catalogs,
                      const Clock &clock = Clock());

    std::string queryCatalog(const std::string &deviceId,
                             std::string *error = nullptr);
    bool handleResponse(const SipMessage &response);
    void sweep();

    bool findCommand(const std::string &commandId, PlatformCommand &command) const;
    std::vector<PlatformCommand> listCommands() const;
    size_t pendingTransactions() const;

private:
    void finishCommand(const std::string &commandId,
                       const SipTransactionResult &result);
    std::string commandToken(const std::string &prefix,
                             uint64_t sequence) const;

    GbSipConfig _config;
    RegistrationStore::Ptr _registrations;
    DeviceCatalogStore::Ptr _catalogs;
    Clock _clock;
    SipClientTransactionStore::Ptr _transactions;
    std::atomic<uint64_t> _sequence;
    mutable std::mutex _mutex;
    std::map<std::string, PlatformCommand> _commands;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_GB_PLATFORM_SERVICE_H
