#ifndef EASY_SVA_GB28181_GB_CONTROL_API_H
#define EASY_SVA_GB28181_GB_CONTROL_API_H

#include <map>
#include <memory>
#include <string>

#include "DeviceCatalogStore.h"
#include "GbLiveService.h"
#include "GbPlatformService.h"
#include "GbSipConfig.h"
#include "RegistrationStore.h"

namespace toolkit {
class TcpServer;
}

namespace easy_sva {
namespace gb28181 {

struct GbControlHttpResult {
    int statusCode;
    std::string body;

    GbControlHttpResult();
};

class GbControlApi : public std::enable_shared_from_this<GbControlApi> {
public:
    typedef std::shared_ptr<GbControlApi> Ptr;
    typedef std::map<std::string, std::string> Parameters;

    GbControlApi(const GbSipConfig &config,
                 const RegistrationStore::Ptr &registrations,
                 const DeviceCatalogStore::Ptr &catalogs,
                 const GbPlatformService::Ptr &platform,
                 const GbLiveService::Ptr &live);
    ~GbControlApi();

    void start();
    void stop();
    bool running() const;

    GbControlHttpResult dispatch(
        const std::string &method,
        const std::string &path,
        const std::string &authorization,
        const Parameters &parameters = Parameters());

private:
    bool authorized(const std::string &authorization) const;

    GbSipConfig _config;
    RegistrationStore::Ptr _registrations;
    DeviceCatalogStore::Ptr _catalogs;
    GbPlatformService::Ptr _platform;
    GbLiveService::Ptr _live;
    std::shared_ptr<toolkit::TcpServer> _server;
    bool _running;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_GB_CONTROL_API_H
