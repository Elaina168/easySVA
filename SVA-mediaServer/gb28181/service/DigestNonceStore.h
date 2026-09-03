#ifndef EASY_SVA_GB28181_DIGEST_NONCE_STORE_H
#define EASY_SVA_GB28181_DIGEST_NONCE_STORE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace easy_sva {
namespace gb28181 {

class DigestNonceStore {
public:
    typedef std::shared_ptr<DigestNonceStore> Ptr;
    typedef std::function<uint64_t()> Clock;
    typedef std::function<std::string()> NonceFactory;

    enum Status {
        Valid,
        Stale,
        Unknown
    };

    explicit DigestNonceStore(uint32_t ttlSeconds);
    DigestNonceStore(uint32_t ttlSeconds,
                     const Clock &clock,
                     const NonceFactory &factory);

    std::string issue(const std::string &deviceId);
    Status validate(const std::string &deviceId, const std::string &nonce);
    size_t prune();
    size_t size() const;

private:
    struct Entry {
        std::string deviceId;
        uint64_t expiresAt;
    };

    uint32_t _ttl_seconds;
    Clock _clock;
    NonceFactory _factory;
    mutable std::mutex _mutex;
    std::map<std::string, Entry> _entries;
};

uint64_t gbSipUnixSeconds();
std::string makeGbSipNonce();

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_DIGEST_NONCE_STORE_H
