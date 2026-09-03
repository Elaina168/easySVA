#include "DeviceCatalogStore.h"

namespace easy_sva {
namespace gb28181 {

DeviceChannel::DeviceChannel() : lastCatalogAt(0) {}

void DeviceCatalogStore::replace(const std::string &parentDeviceId,
                                 const std::vector<DeviceChannel> &channels,
                                 uint64_t now) {
    ChannelMap replacement;
    for (std::vector<DeviceChannel>::const_iterator it = channels.begin();
         it != channels.end(); ++it) {
        DeviceChannel channel = *it;
        channel.lastCatalogAt = now;
        replacement[channel.deviceId] = channel;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _catalogs[parentDeviceId].swap(replacement);
}

void DeviceCatalogStore::upsert(const std::string &parentDeviceId,
                                const std::vector<DeviceChannel> &channels,
                                uint64_t now) {
    std::lock_guard<std::mutex> lock(_mutex);
    ChannelMap &catalog = _catalogs[parentDeviceId];
    for (std::vector<DeviceChannel>::const_iterator it = channels.begin();
         it != channels.end(); ++it) {
        DeviceChannel channel = *it;
        channel.lastCatalogAt = now;
        catalog[channel.deviceId] = channel;
    }
}

bool DeviceCatalogStore::find(const std::string &parentDeviceId,
                              const std::string &channelId,
                              DeviceChannel &channel) const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, ChannelMap>::const_iterator catalog = _catalogs.find(parentDeviceId);
    if (catalog == _catalogs.end()) {
        return false;
    }
    ChannelMap::const_iterator found = catalog->second.find(channelId);
    if (found == catalog->second.end()) {
        return false;
    }
    channel = found->second;
    return true;
}

std::vector<DeviceChannel> DeviceCatalogStore::list(const std::string &parentDeviceId) const {
    std::vector<DeviceChannel> channels;
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, ChannelMap>::const_iterator catalog = _catalogs.find(parentDeviceId);
    if (catalog == _catalogs.end()) {
        return channels;
    }
    channels.reserve(catalog->second.size());
    for (ChannelMap::const_iterator it = catalog->second.begin();
         it != catalog->second.end(); ++it) {
        channels.push_back(it->second);
    }
    return channels;
}

bool DeviceCatalogStore::removeDevice(const std::string &parentDeviceId) {
    std::lock_guard<std::mutex> lock(_mutex);
    return _catalogs.erase(parentDeviceId) != 0;
}

size_t DeviceCatalogStore::size(const std::string &parentDeviceId) const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, ChannelMap>::const_iterator catalog = _catalogs.find(parentDeviceId);
    return catalog == _catalogs.end() ? 0 : catalog->second.size();
}

} // namespace gb28181
} // namespace easy_sva
