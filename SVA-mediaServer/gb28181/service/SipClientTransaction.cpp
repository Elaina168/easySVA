#include "SipClientTransaction.h"

#include <sstream>
#include <utility>
#include <vector>

#include "DigestNonceStore.h"

namespace easy_sva {
namespace gb28181 {

SipTransactionResult::SipTransactionResult() : timedOut(false), sendFailed(false) {}

SipClientTransactionStore::SipClientTransactionStore(const Clock &clock)
    : _clock(clock ? clock : Clock(gbSipUnixSeconds)) {}

void SipClientTransactionStore::setError(std::string *error, const std::string &value) {
    if (error) {
        *error = value;
    }
}

bool SipClientTransactionStore::transactionKey(const SipMessage &message,
                                               std::string &key,
                                               std::string *error) {
    const std::string callId = message.header("Call-ID");
    const std::string cseq = message.header("CSeq");
    if (callId.empty() || cseq.empty()) {
        setError(error, "SIP client transaction requires Call-ID and CSeq");
        return false;
    }
    std::istringstream input(cseq);
    uint64_t sequence = 0;
    std::string method;
    std::string trailing;
    if (!(input >> sequence >> method) || (input >> trailing)) {
        setError(error, "invalid SIP CSeq for client transaction");
        return false;
    }
    key = callId + "\n" + std::to_string(sequence) + "\n" + method;
    return true;
}

bool SipClientTransactionStore::send(const SipMessage &request,
                                     const Sender &sender,
                                     uint32_t timeoutSeconds,
                                     const Completion &completion,
                                     std::string *error) {
    if (error) {
        error->clear();
    }
    if (!request.isRequest()) {
        setError(error, "SIP client transaction can only send a request");
        return false;
    }
    if (!sender) {
        setError(error, "registered device has no active signaling sender");
        return false;
    }
    if (timeoutSeconds == 0) {
        setError(error, "SIP client transaction timeout must be positive");
        return false;
    }

    std::string key;
    if (!transactionKey(request, key, error)) {
        return false;
    }
    Entry entry;
    entry.expiresAt = _clock() + timeoutSeconds;
    entry.completion = completion;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_entries.find(key) != _entries.end()) {
            setError(error, "duplicate SIP client transaction");
            return false;
        }
        _entries[key] = entry;
    }

    if (sender(request.serialize())) {
        return true;
    }

    Completion callback;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::map<std::string, Entry>::iterator found = _entries.find(key);
        if (found != _entries.end()) {
            callback = found->second.completion;
            _entries.erase(found);
        }
    }
    const std::string message = "failed to send SIP request to registered device";
    setError(error, message);
    if (callback) {
        SipTransactionResult result;
        result.sendFailed = true;
        result.error = message;
        callback(result);
    }
    return false;
}

bool SipClientTransactionStore::handleResponse(const SipMessage &response) {
    if (response.isRequest() || response.statusCode() < 200) {
        return false;
    }
    std::string key;
    if (!transactionKey(response, key, nullptr)) {
        return false;
    }

    Completion callback;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::map<std::string, Entry>::iterator found = _entries.find(key);
        if (found == _entries.end()) {
            return false;
        }
        callback = found->second.completion;
        _entries.erase(found);
    }
    if (callback) {
        SipTransactionResult result;
        result.response = response;
        callback(result);
    }
    return true;
}

bool SipClientTransactionStore::cancel(const SipMessage &request) {
    std::string key;
    if (!transactionKey(request, key, nullptr)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _entries.erase(key) != 0;
}

size_t SipClientTransactionStore::expire() {
    const uint64_t now = _clock();
    std::vector<Completion> callbacks;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (std::map<std::string, Entry>::iterator it = _entries.begin();
             it != _entries.end();) {
            if (now >= it->second.expiresAt) {
                callbacks.push_back(it->second.completion);
                it = _entries.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (std::vector<Completion>::const_iterator it = callbacks.begin();
         it != callbacks.end(); ++it) {
        if (*it) {
            SipTransactionResult result;
            result.timedOut = true;
            result.error = "SIP client transaction timed out";
            (*it)(result);
        }
    }
    return callbacks.size();
}

size_t SipClientTransactionStore::size() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _entries.size();
}

} // namespace gb28181
} // namespace easy_sva
