#ifndef EASY_SVA_GB28181_DEVICE_CATALOG_STORE_H
#define EASY_SVA_GB28181_DEVICE_CATALOG_STORE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace easy_sva {
namespace gb28181 {

struct DeviceChannel {
    std::string deviceId;
    std::string name;
    std::string manufacturer;
    std::string model;
    std::string owner;
    std::string civilCode;
    std::string address;
    std::string parental;
    std::string parentId;
    std::string safetyWay;
    std::string registerWay;
    std::string secrecy;
    std::string status;
    std::string longitude;
    std::string latitude;
    uint64_t lastCatalogAt;

    DeviceChannel();
};

class DeviceCatalogStore {
public:
    typedef std::shared_ptr<DeviceCatalogStore> Ptr;

    void replace(const std::string &parentDeviceId,
                 const std::vector<DeviceChannel> &channels,
                 uint64_t now);
    void upsert(const std::string &parentDeviceId,
                const std::vector<DeviceChannel> &channels,
                uint64_t now);
    bool find(const std::string &parentDeviceId,
              const std::string &channelId,
              DeviceChannel &channel) const;
    std::vector<DeviceChannel> list(const std::string &parentDeviceId) const;
    bool removeDevice(const std::string &parentDeviceId);
    size_t size(const std::string &parentDeviceId) const;

private:
    typedef std::map<std::string, DeviceChannel> ChannelMap;

    mutable std::mutex _mutex;
    std::map<std::string, ChannelMap> _catalogs;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_DEVICE_CATALOG_STORE_H
