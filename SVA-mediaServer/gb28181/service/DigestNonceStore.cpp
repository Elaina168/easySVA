#include "DigestNonceStore.h"

#include <atomic>
#include <chrono>
#include <random>
#include <sstream>

#include "Util/MD5.h"

namespace easy_sva {
namespace gb28181 {

uint64_t gbSipUnixSeconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string makeGbSipNonce() {
    static std::atomic<uint64_t> sequence(0);
    std::random_device random;
    std::ostringstream seed;
    seed << std::chrono::high_resolution_clock::now().time_since_epoch().count()
         << ':' << sequence.fetch_add(1) << ':' << random() << ':' << random();
    return toolkit::MD5(seed.str()).hexdigest();
}

DigestNonceStore::DigestNonceStore(uint32_t ttlSeconds)
    : _ttl_seconds(ttlSeconds), _clock(gbSipUnixSeconds), _factory(makeGbSipNonce) {}

DigestNonceStore::DigestNonceStore(uint32_t ttlSeconds,
                                   const Clock &clock,
                                   const NonceFactory &factory)
    : _ttl_seconds(ttlSeconds), _clock(clock), _factory(factory) {}

std::string DigestNonceStore::issue(const std::string &deviceId) {
    const std::string nonce = _factory();
    Entry entry;
    entry.deviceId = deviceId;
    entry.expiresAt = _clock() + _ttl_seconds;
    std::lock_guard<std::mutex> lock(_mutex);
    _entries[nonce] = entry;
    return nonce;
}

DigestNonceStore::Status DigestNonceStore::validate(const std::string &deviceId,
                                                    const std::string &nonce) {
    const uint64_t now = _clock();
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, Entry>::iterator it = _entries.find(nonce);
    if (it == _entries.end() || it->second.deviceId != deviceId) {
        return Unknown;
    }
    if (now >= it->second.expiresAt) {
        _entries.erase(it);
        return Stale;
    }
    return Valid;
}

size_t DigestNonceStore::prune() {
    const uint64_t now = _clock();
    size_t removed = 0;
    std::lock_guard<std::mutex> lock(_mutex);
    for (std::map<std::string, Entry>::iterator it = _entries.begin(); it != _entries.end();) {
        if (now >= it->second.expiresAt) {
            it = _entries.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

size_t DigestNonceStore::size() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _entries.size();
}

} // namespace gb28181
} // namespace easy_sva
