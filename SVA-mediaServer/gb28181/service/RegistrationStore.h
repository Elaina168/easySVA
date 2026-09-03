#ifndef EASY_SVA_GB28181_REGISTRATION_STORE_H
#define EASY_SVA_GB28181_REGISTRATION_STORE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace easy_sva {
namespace gb28181 {

struct RegisteredDevice {
    std::string deviceId;
    std::string contact;
    std::string transport;
    std::string peerIp;
    uint16_t peerPort;
    std::string userAgent;
    std::string callId;
    uint64_t registeredAt;
    uint64_t lastRegisterAt;
    uint64_t expiresAt;

    RegisteredDevice();
};

class RegistrationStore {
public:
    typedef std::shared_ptr<RegistrationStore> Ptr;

    void upsert(const RegisteredDevice &device);
    bool remove(const std::string &deviceId);
    bool find(const std::string &deviceId, RegisteredDevice &device) const;
    std::vector<RegisteredDevice> list() const;
    size_t expire(uint64_t now);
    size_t size() const;

private:
    mutable std::mutex _mutex;
    std::map<std::string, RegisteredDevice> _devices;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_REGISTRATION_STORE_H
