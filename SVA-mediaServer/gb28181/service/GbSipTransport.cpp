#include "GbSipTransport.h"

#include <stdexcept>
#include <string>

#include "Network/Session.h"
#include "Network/TcpServer.h"
#include "Network/UdpServer.h"
#include "Util/TimeTicker.h"
#include "Util/logger.h"
#include "core/SipResponse.h"
#include "core/SipStreamDecoder.h"

namespace easy_sva {
namespace gb28181 {
namespace {

SipPeer makePeer(toolkit::Session &session, const std::string &transport) {
    SipPeer peer;
    peer.transport = transport;
    peer.ip = session.get_peer_ip();
    peer.port = session.get_peer_port();
    const std::weak_ptr<toolkit::SocketHelper> weakSender = session.shared_from_this();
    peer.sender = [weakSender](const std::string &wire) {
        const std::shared_ptr<toolkit::SocketHelper> sender = weakSender.lock();
        return sender && sender->send(wire) >= 0;
    };
    return peer;
}

class GbSipUdpSession : public toolkit::Session {
public:
    explicit GbSipUdpSession(const toolkit::Socket::Ptr &socket)
        : toolkit::Session(socket), _max_message_bytes(1024 * 1024), _idle_timeout_ms(180000) {}

    void configure(const SipRequestProcessor::Ptr &processor,
                   size_t maxMessageBytes,
                   uint32_t idleTimeoutSeconds) {
        _processor = processor;
        _max_message_bytes = maxMessageBytes;
        _idle_timeout_ms = static_cast<uint64_t>(idleTimeoutSeconds) * 1000;
    }

    void onRecv(const toolkit::Buffer::Ptr &buffer) override {
        _idle.resetTime();
        if (buffer->size() > _max_message_bytes) {
            send(SipResponse::badRequest().serialize());
            return;
        }

        SipMessage message;
        size_t consumed = 0;
        std::string error;
        const std::string wire(buffer->data(), buffer->size());
        if (!SipMessage::parse(wire, message, &consumed, &error) || consumed != wire.size()) {
            WarnL << "Invalid GB28181 SIP/UDP datagram from " << get_peer_ip()
                  << ":" << get_peer_port() << ": " << error;
            send(SipResponse::badRequest().serialize());
            return;
        }
        dispatch(message);
    }

    void onError(const toolkit::SockException &error) override {
        DebugL << "GB28181 SIP/UDP peer closed: " << error;
    }

    void onManager() override {
        if (_idle.elapsedTime() > _idle_timeout_ms) {
            safeShutdown(toolkit::SockException(toolkit::Err_timeout, "GB28181 SIP/UDP peer idle timeout"));
        }
    }

private:
    void dispatch(const SipMessage &message) {
        SipMessage response;
        if (_processor && _processor->process(message, makePeer(*this, "UDP"), response)) {
            send(response.serialize());
        }
    }

    SipRequestProcessor::Ptr _processor;
    size_t _max_message_bytes;
    uint64_t _idle_timeout_ms;
    toolkit::Ticker _idle;
};

class GbSipTcpSession : public toolkit::Session {
public:
    explicit GbSipTcpSession(const toolkit::Socket::Ptr &socket)
        : toolkit::Session(socket), _decoder(1024 * 1024), _idle_timeout_ms(180000) {}

    void configure(const SipRequestProcessor::Ptr &processor,
                   size_t maxMessageBytes,
                   uint32_t idleTimeoutSeconds) {
        _processor = processor;
        _decoder = SipStreamDecoder(maxMessageBytes);
        _idle_timeout_ms = static_cast<uint64_t>(idleTimeoutSeconds) * 1000;
    }

    void onRecv(const toolkit::Buffer::Ptr &buffer) override {
        _idle.resetTime();
        std::string error;
        if (!_decoder.append(buffer->data(), buffer->size(), &error)) {
            WarnL << "Oversized GB28181 SIP/TCP stream from " << get_peer_ip()
                  << ":" << get_peer_port();
            send(SipResponse::badRequest().serialize());
            safeShutdown(toolkit::SockException(toolkit::Err_shutdown, error));
            return;
        }

        while (true) {
            SipMessage message;
            const SipStreamDecoder::Result result = _decoder.next(message, &error);
            if (result == SipStreamDecoder::NeedMoreData) {
                return;
            }
            if (result == SipStreamDecoder::InvalidMessage) {
                WarnL << "Invalid GB28181 SIP/TCP message from " << get_peer_ip()
                      << ":" << get_peer_port() << ": " << error;
                send(SipResponse::badRequest().serialize());
                _decoder.clear();
                safeShutdown(toolkit::SockException(toolkit::Err_shutdown, error));
                return;
            }
            dispatch(message);
        }
    }

    void onError(const toolkit::SockException &error) override {
        DebugL << "GB28181 SIP/TCP peer closed: " << error;
    }

    void onManager() override {
        if (_idle.elapsedTime() > _idle_timeout_ms) {
            safeShutdown(toolkit::SockException(toolkit::Err_timeout, "GB28181 SIP/TCP peer idle timeout"));
        }
    }

private:
    void dispatch(const SipMessage &message) {
        SipMessage response;
        if (_processor && _processor->process(message, makePeer(*this, "TCP"), response)) {
            send(response.serialize());
        }
    }

    SipRequestProcessor::Ptr _processor;
    SipStreamDecoder _decoder;
    uint64_t _idle_timeout_ms;
    toolkit::Ticker _idle;
};

} // namespace

GbSipTransportServer::GbSipTransportServer() {}

GbSipTransportServer::~GbSipTransportServer() {
    stop();
}

void GbSipTransportServer::start(const GbSipConfig &config,
                                 const SipRequestProcessor::Ptr &processor) {
    if (running()) {
        throw std::logic_error("GB28181 SIP transport is already running");
    }
    std::string error;
    if (!config.validate(&error)) {
        throw std::invalid_argument(error);
    }
    if (!processor) {
        throw std::invalid_argument("GB28181 SIP request processor is required");
    }

    try {
        if (config.enableUdp) {
            _udp_server.reset(new toolkit::UdpServer());
            _udp_server->start<GbSipUdpSession>(
                config.sipPort, config.listenIp,
                [processor, config](std::shared_ptr<GbSipUdpSession> &session) {
                    session->configure(processor, config.maxMessageBytes, config.idleTimeoutSeconds);
                });
        }
        if (config.enableTcp) {
            _tcp_server.reset(new toolkit::TcpServer());
            _tcp_server->start<GbSipTcpSession>(
                config.sipPort, config.listenIp, 1024,
                [processor, config](std::shared_ptr<GbSipTcpSession> &session) {
                    session->configure(processor, config.maxMessageBytes, config.idleTimeoutSeconds);
                });
        }
    } catch (...) {
        stop();
        throw;
    }

    InfoL << "GB28181 SIP service listening on " << config.listenIp << ":"
          << config.sipPort << " UDP=" << config.enableUdp << " TCP=" << config.enableTcp;
}

void GbSipTransportServer::stop() {
    _tcp_server.reset();
    _udp_server.reset();
}

bool GbSipTransportServer::running() const {
    return static_cast<bool>(_udp_server) || static_cast<bool>(_tcp_server);
}

} // namespace gb28181
} // namespace easy_sva
