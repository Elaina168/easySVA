#ifndef EASY_SVA_GB28181_SIP_CLIENT_TRANSACTION_H
#define EASY_SVA_GB28181_SIP_CLIENT_TRANSACTION_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "core/SipMessage.h"

namespace easy_sva {
namespace gb28181 {

struct SipTransactionResult {
    bool timedOut;
    bool sendFailed;
    SipMessage response;
    std::string error;

    SipTransactionResult();
};

class SipClientTransactionStore {
public:
    typedef std::shared_ptr<SipClientTransactionStore> Ptr;
    typedef std::function<uint64_t()> Clock;
    typedef std::function<bool(const std::string &)> Sender;
    typedef std::function<void(const SipTransactionResult &)> Completion;

    explicit SipClientTransactionStore(const Clock &clock);

    bool send(const SipMessage &request,
              const Sender &sender,
              uint32_t timeoutSeconds,
              const Completion &completion,
              std::string *error = nullptr);
    bool handleResponse(const SipMessage &response);
    size_t expire();
    size_t size() const;

private:
    struct Entry {
        uint64_t expiresAt;
        Completion completion;
    };

    static bool transactionKey(const SipMessage &message,
                               std::string &key,
                               std::string *error);
    static void setError(std::string *error, const std::string &value);

    Clock _clock;
    mutable std::mutex _mutex;
    std::map<std::string, Entry> _entries;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_SIP_CLIENT_TRANSACTION_H
