#ifndef EASY_SVA_GB28181_GB_SIP_TRANSPORT_H
#define EASY_SVA_GB28181_GB_SIP_TRANSPORT_H

#include <memory>

#include "GbSipConfig.h"
#include "SipRequestProcessor.h"

namespace toolkit {
class TcpServer;
class UdpServer;
}

namespace easy_sva {
namespace gb28181 {

class GbSipTransportServer {
public:
    GbSipTransportServer();
    ~GbSipTransportServer();

    void start(const GbSipConfig &config,
               const SipRequestProcessor::Ptr &processor);
    void stop();
    bool running() const;

private:
    GbSipTransportServer(const GbSipTransportServer &);
    GbSipTransportServer &operator=(const GbSipTransportServer &);

    std::shared_ptr<toolkit::UdpServer> _udp_server;
    std::shared_ptr<toolkit::TcpServer> _tcp_server;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_GB_SIP_TRANSPORT_H
