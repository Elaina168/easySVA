#include "RegistrationStore.h"

namespace easy_sva {
namespace gb28181 {

RegisteredDevice::RegisteredDevice()
    : peerPort(0), registeredAt(0), lastRegisterAt(0), expiresAt(0) {}

void RegistrationStore::upsert(const RegisteredDevice &device) {
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, RegisteredDevice>::iterator existing = _devices.find(device.deviceId);
    RegisteredDevice updated = device;
    if (existing != _devices.end()) {
        updated.registeredAt = existing->second.registeredAt;
    }
    _devices[device.deviceId] = updated;
}

bool RegistrationStore::remove(const std::string &deviceId) {
    std::lock_guard<std::mutex> lock(_mutex);
    return _devices.erase(deviceId) != 0;
}

bool RegistrationStore::find(const std::string &deviceId, RegisteredDevice &device) const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, RegisteredDevice>::const_iterator it = _devices.find(deviceId);
    if (it == _devices.end()) {
        return false;
    }
    device = it->second;
    return true;
}

std::vector<RegisteredDevice> RegistrationStore::list() const {
    std::vector<RegisteredDevice> devices;
    std::lock_guard<std::mutex> lock(_mutex);
    devices.reserve(_devices.size());
    for (std::map<std::string, RegisteredDevice>::const_iterator it = _devices.begin();
         it != _devices.end(); ++it) {
        devices.push_back(it->second);
    }
    return devices;
}

size_t RegistrationStore::expire(uint64_t now) {
    size_t removed = 0;
    std::lock_guard<std::mutex> lock(_mutex);
    for (std::map<std::string, RegisteredDevice>::iterator it = _devices.begin();
         it != _devices.end();) {
        if (now >= it->second.expiresAt) {
            it = _devices.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

size_t RegistrationStore::size() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _devices.size();
}

} // namespace gb28181
} // namespace easy_sva
