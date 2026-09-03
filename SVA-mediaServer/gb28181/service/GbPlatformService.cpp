#include "GbPlatformService.h"

#include "DigestNonceStore.h"
#include "SipRequestFactory.h"

namespace easy_sva {
namespace gb28181 {
namespace {

void setError(std::string *error, const std::string &value) {
    if (error) {
        *error = value;
    }
}

} // namespace

PlatformCommand::PlatformCommand()
    : state(PlatformCommandPending), sipStatus(0), createdAt(0), completedAt(0) {}

GbPlatformService::GbPlatformService(const GbSipConfig &config,
                                     const RegistrationStore::Ptr &registrations,
                                     const DeviceCatalogStore::Ptr &catalogs,
                                     const Clock &clock)
    : _config(config),
      _registrations(registrations ? registrations : RegistrationStore::Ptr(new RegistrationStore())),
      _catalogs(catalogs ? catalogs : DeviceCatalogStore::Ptr(new DeviceCatalogStore())),
      _clock(clock ? clock : Clock(gbSipUnixSeconds)),
      _transactions(new SipClientTransactionStore(_clock)),
      _sequence(0) {}

std::string GbPlatformService::commandToken(const std::string &prefix,
                                            uint64_t sequence) const {
    return prefix + "-" + std::to_string(_clock()) + "-" +
        std::to_string(sequence);
}

std::string GbPlatformService::queryCatalog(const std::string &deviceId,
                                            std::string *error) {
    if (error) {
        error->clear();
    }
    RegisteredDevice device;
    if (!_registrations->find(deviceId, device)) {
        setError(error, "GB28181 device is not registered");
        return std::string();
    }
    if (!device.online) {
        setError(error, "GB28181 device is offline");
        return std::string();
    }
    if (!device.sender) {
        setError(error, "GB28181 device has no active signaling channel");
        return std::string();
    }

    const uint64_t sequence = _sequence.fetch_add(1) + 1;
    const std::string commandId = commandToken("catalog", sequence);
    const SipMessage request = SipRequestFactory::catalogQuery(
        _config, device, sequence, sequence, commandId);

    PlatformCommand command;
    command.commandId = commandId;
    command.type = "Catalog";
    command.deviceId = deviceId;
    command.createdAt = _clock();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _commands[commandId] = command;
    }

    std::weak_ptr<GbPlatformService> weakSelf;
    try {
        weakSelf = shared_from_this();
    } catch (const std::bad_weak_ptr &) {
        std::lock_guard<std::mutex> lock(_mutex);
        _commands.erase(commandId);
        setError(error, "GB28181 platform service must be managed by shared_ptr");
        return std::string();
    }
    const bool sent = _transactions->send(
        request, device.sender, _config.transactionTimeoutSeconds,
        [weakSelf, commandId](const SipTransactionResult &result) {
            const std::shared_ptr<GbPlatformService> self = weakSelf.lock();
            if (self) {
                self->finishCommand(commandId, result);
            }
        }, error);
    if (!sent) {
        return std::string();
    }
    return commandId;
}

void GbPlatformService::finishCommand(const std::string &commandId,
                                      const SipTransactionResult &result) {
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, PlatformCommand>::iterator found = _commands.find(commandId);
    if (found == _commands.end()) {
        return;
    }
    found->second.completedAt = _clock();
    found->second.sipStatus = result.response.statusCode();
    found->second.error = result.error;
    if (result.timedOut) {
        found->second.state = PlatformCommandTimedOut;
    } else if (result.sendFailed || result.response.statusCode() < 200 ||
               result.response.statusCode() >= 300) {
        found->second.state = PlatformCommandFailed;
        if (found->second.error.empty()) {
            found->second.error = "device returned SIP status " +
                std::to_string(result.response.statusCode());
        }
    } else {
        found->second.state = PlatformCommandSucceeded;
    }
}

bool GbPlatformService::handleResponse(const SipMessage &response) {
    return _transactions->handleResponse(response);
}

void GbPlatformService::sweep() {
    _transactions->expire();
}

bool GbPlatformService::findCommand(const std::string &commandId,
                                    PlatformCommand &command) const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<std::string, PlatformCommand>::const_iterator found = _commands.find(commandId);
    if (found == _commands.end()) {
        return false;
    }
    command = found->second;
    return true;
}

std::vector<PlatformCommand> GbPlatformService::listCommands() const {
    std::vector<PlatformCommand> commands;
    std::lock_guard<std::mutex> lock(_mutex);
    commands.reserve(_commands.size());
    for (std::map<std::string, PlatformCommand>::const_iterator it = _commands.begin();
         it != _commands.end(); ++it) {
        commands.push_back(it->second);
    }
    return commands;
}

size_t GbPlatformService::pendingTransactions() const {
    return _transactions->size();
}

} // namespace gb28181
} // namespace easy_sva
