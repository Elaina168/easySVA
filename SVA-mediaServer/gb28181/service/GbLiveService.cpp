#include "GbLiveService.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "DigestNonceStore.h"
#include "GbSdp.h"
#include "SipRequestFactory.h"

namespace easy_sva {
namespace gb28181 {
namespace {

void setError(std::string *error, const std::string &value) {
    if (error) {
        *error = value;
    }
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return ch;
    });
    return value;
}

bool isGbId(const std::string &value) {
    if (value.size() != 20) {
        return false;
    }
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (*it < '0' || *it > '9') {
            return false;
        }
    }
    return true;
}

bool hasTag(const std::string &header) {
    return lowerAscii(header).find(";tag=") != std::string::npos;
}

std::string transactionFailure(const SipTransactionResult &result,
                               const std::string &method) {
    if (result.timedOut || result.sendFailed) {
        return result.error;
    }
    const int status = result.response.statusCode();
    if (status < 200 || status >= 300) {
        return "device returned SIP " + std::to_string(status) + " to " + method;
    }
    return std::string();
}

bool parseCseqMethod(const SipMessage &message, std::string &method) {
    std::istringstream input(message.header("CSeq"));
    uint64_t sequence = 0;
    std::string trailing;
    return (input >> sequence >> method) && !(input >> trailing);
}

} // namespace

GbLiveService::MediaDialog::MediaDialog() : nextCseq(0) {}

GbLiveService::GbLiveService(const GbSipConfig &config,
                             const RegistrationStore::Ptr &registrations,
                             const DeviceCatalogStore::Ptr &catalogs,
                             const Clock &clock,
                             const ZlmApiClient::Requester &zlmRequester)
    : _config(config),
      _registrations(registrations ? registrations : RegistrationStore::Ptr(new RegistrationStore())),
      _catalogs(catalogs ? catalogs : DeviceCatalogStore::Ptr(new DeviceCatalogStore())),
      _clock(clock ? clock : Clock(gbSipUnixSeconds)),
      _transactions(new SipClientTransactionStore(_clock)),
      _sessions(new MediaSessionStore()),
      _zlm(new ZlmApiClient(config, zlmRequester)),
      _sequence(0) {}

std::string GbLiveService::token(const std::string &prefix,
                                 uint64_t sequence) const {
    return prefix + "-" + std::to_string(_clock()) + "-" +
        std::to_string(sequence);
}

std::string GbLiveService::ssrcForSequence(uint64_t sequence) {
    const uint32_t numeric = 100000000u +
        static_cast<uint32_t>(sequence % 900000000u);
    std::ostringstream output;
    output << std::setw(10) << std::setfill('0') << numeric;
    return output.str();
}

std::string GbLiveService::startLive(const std::string &deviceId,
                                     const std::string &channelId,
                                     std::string *error) {
    if (error) {
        error->clear();
    }
    if (!isGbId(deviceId) || !isGbId(channelId)) {
        setError(error, "GB28181 device and channel IDs must contain 20 decimal digits");
        return std::string();
    }
    if (_config.rtpTcpMode == 2) {
        setError(error, "GB28181 TCP active media requires connectRtpServer support");
        return std::string();
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
    DeviceChannel channel;
    if (!_catalogs->find(deviceId, channelId, channel)) {
        setError(error, "GB28181 channel is not present in the device catalog");
        return std::string();
    }
    if (lowerAscii(channel.status) == "off") {
        setError(error, "GB28181 channel is offline");
        return std::string();
    }

    std::weak_ptr<GbLiveService> weakSelf;
    try {
        weakSelf = shared_from_this();
    } catch (const std::bad_weak_ptr &) {
        setError(error, "GB28181 live service must be managed by shared_ptr");
        return std::string();
    }

    const uint64_t sequence = _sequence.fetch_add(1) + 1;
    const std::string sessionId = token("live", sequence);
    const std::string ssrc = ssrcForSequence(sequence);
    GbMediaSession session;
    session.sessionId = sessionId;
    session.deviceId = deviceId;
    session.channelId = channelId;
    session.streamId = "gb_" + channelId + "_" + ssrc;
    session.callId = sessionId + "@" + _config.advertisedIp;
    session.ssrc = ssrc;
    session.tcp = _config.rtpTcpMode != 0;
    session.createdAt = _clock();
    session.updatedAt = session.createdAt;
    if (!_sessions->create(session, error)) {
        return std::string();
    }

    uint32_t numericSsrc = 0;
    std::string ssrcError;
    if (!GbSdp::parseSsrc(ssrc, numericSsrc, &ssrcError)) {
        failBeforeDialog(sessionId, GbMediaPreparing, ssrcError, false);
        setError(error, ssrcError);
        return sessionId;
    }
    ZlmRtpOpenOptions options;
    options.streamId = session.streamId;
    options.localIp = _config.rtpListenIp;
    options.requestedPort = _config.rtpPort;
    options.ssrc = numericSsrc;
    options.tcpMode = _config.rtpTcpMode;
    const std::string streamId = session.streamId;
    _zlm->openRtpServer(options,
        [weakSelf, sessionId, streamId, device, sequence](const ZlmRtpOpenResult &result) {
            const Ptr self = weakSelf.lock();
            if (self) {
                self->onRtpOpened(sessionId, streamId, device, sequence, result);
            }
        });
    return sessionId;
}

void GbLiveService::onRtpOpened(const std::string &sessionId,
                                const std::string &streamId,
                                const RegisteredDevice &device,
                                uint64_t inviteCseq,
                                const ZlmRtpOpenResult &result) {
    if (!result.ok) {
        failBeforeDialog(sessionId, GbMediaPreparing, result.error, false);
        return;
    }
    std::string error;
    if (!_sessions->assignRtpPort(sessionId, result.port, _clock(), &error)) {
        failBeforeDialog(sessionId, GbMediaPreparing, error, true);
        return;
    }
    GbMediaSession session;
    if (!_sessions->find(sessionId, session)) {
        _zlm->closeRtpServer(streamId,
            [](const ZlmRtpCloseResult &) {});
        return;
    }
    GbSdpOffer offer;
    offer.platformId = _config.serverId;
    offer.channelId = session.channelId;
    offer.destinationIp = _config.rtpAdvertisedIp.empty()
        ? _config.advertisedIp : _config.rtpAdvertisedIp;
    offer.destinationPort = session.rtpPort;
    offer.ssrc = session.ssrc;
    offer.tcpPassive = _config.rtpTcpMode == 1;
    std::string sdp;
    if (!GbSdp::buildPlayOffer(offer, sdp, &error)) {
        failBeforeDialog(sessionId, GbMediaPreparing, error, true);
        return;
    }
    const SipMessage invite = SipRequestFactory::liveInvite(
        _config, device, session.channelId, sdp, session.ssrc,
        inviteCseq, sessionId);
    if (!_sessions->transition(sessionId, GbMediaPreparing, GbMediaInviting,
                               _clock(), std::string(), &error)) {
        failBeforeDialog(sessionId, GbMediaPreparing, error, true);
        return;
    }

    std::weak_ptr<GbLiveService> weakSelf = shared_from_this();
    _transactions->send(invite, device.sender, _config.transactionTimeoutSeconds,
        [weakSelf, sessionId, device, invite, inviteCseq](
                const SipTransactionResult &transactionResult) {
            const Ptr self = weakSelf.lock();
            if (self) {
                self->finishInvite(sessionId, device, invite,
                                   inviteCseq, transactionResult);
            }
        }, &error);
}

bool GbLiveService::validateAcceptedSdp(const GbMediaSession &session,
                                        const SipMessage &response,
                                        std::string &error) const {
    if (!hasTag(response.header("To"))) {
        error = "accepted GB28181 INVITE response has no To tag";
        return false;
    }
    const std::string contentType = lowerAscii(response.header("Content-Type"));
    if (contentType.compare(0, 15, "application/sdp") != 0) {
        error = "accepted GB28181 INVITE response is not application/sdp";
        return false;
    }
    if (response.body().empty()) {
        error = "accepted GB28181 INVITE response has no SDP body";
        return false;
    }
    GbSdpDescription answer;
    if (!GbSdp::parse(response.body(), answer, &error)) {
        return false;
    }
    if (!answer.supportsPs()) {
        error = "GB28181 device SDP does not offer PS/90000";
        return false;
    }
    if (answer.ssrc != session.ssrc) {
        error = "GB28181 device SDP SSRC does not match the INVITE";
        return false;
    }
    if (answer.isTcp() != session.tcp) {
        error = "GB28181 device SDP transport does not match the INVITE";
        return false;
    }
    if (answer.direction == "recvonly" || answer.direction == "inactive") {
        error = "GB28181 device SDP does not send media";
        return false;
    }
    if (session.tcp && !answer.setup.empty() &&
        lowerAscii(answer.setup) != "active") {
        error = "GB28181 TCP device SDP must answer with active setup";
        return false;
    }
    return true;
}

void GbLiveService::finishInvite(const std::string &sessionId,
                                 const RegisteredDevice &device,
                                 const SipMessage &invite,
                                 uint64_t inviteCseq,
                                 const SipTransactionResult &result) {
    const int status = result.response.statusCode();
    bool ackSent = true;
    if (status >= 200) {
        const SipMessage ack = SipRequestFactory::inviteAck(
            _config, invite, result.response,
            token("ack", _sequence.fetch_add(1) + 1));
        ackSent = device.sender && device.sender(ack.serialize());
    }

    const std::string transactionError = transactionFailure(result, "INVITE");
    if (!transactionError.empty()) {
        std::string detail = transactionError;
        if (status >= 200 && !ackSent) {
            detail += "; failed to send ACK";
        }
        failBeforeDialog(sessionId, GbMediaInviting, detail, true);
        return;
    }

    GbMediaSession session;
    if (!_sessions->find(sessionId, session)) {
        return;
    }
    MediaDialog dialog;
    dialog.invite = invite;
    dialog.acceptedResponse = result.response;
    dialog.sender = device.sender;
    dialog.nextCseq = inviteCseq + 1;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _dialogs[sessionId] = dialog;
    }

    std::string validationError;
    validateAcceptedSdp(session, result.response, validationError);
    if (!ackSent && validationError.empty()) {
        validationError = "failed to send ACK for accepted GB28181 INVITE";
    }
    if (!validationError.empty()) {
        std::string transitionError;
        if (_sessions->transition(sessionId, GbMediaInviting, GbMediaStopping,
                                  _clock(), std::string(), &transitionError)) {
            beginBye(sessionId, validationError, nullptr);
        } else {
            failBeforeDialog(sessionId, GbMediaInviting,
                             validationError + "; " + transitionError, true);
        }
        return;
    }

    std::string transitionError;
    if (!_sessions->transition(sessionId, GbMediaInviting, GbMediaStreaming,
                               _clock(), std::string(), &transitionError)) {
        beginBye(sessionId, transitionError, nullptr);
    }
}

bool GbLiveService::stopLive(const std::string &sessionId,
                             std::string *error) {
    if (error) {
        error->clear();
    }
    GbMediaSession session;
    if (!_sessions->find(sessionId, session)) {
        setError(error, "GB28181 media session was not found");
        return false;
    }
    if (session.state != GbMediaStreaming) {
        setError(error, "only a streaming GB28181 media session can be stopped");
        return false;
    }
    if (!_sessions->transition(sessionId, GbMediaStreaming, GbMediaStopping,
                               _clock(), std::string(), error)) {
        return false;
    }
    return beginBye(sessionId, std::string(), error);
}

bool GbLiveService::beginBye(const std::string &sessionId,
                             const std::string &failureReason,
                             std::string *error) {
    MediaDialog dialog;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::map<std::string, MediaDialog>::iterator found = _dialogs.find(sessionId);
        if (found == _dialogs.end()) {
            const std::string detail = "GB28181 media dialog was not found";
            setError(error, detail);
            GbMediaSession session;
            if (_sessions->find(sessionId, session)) {
                _sessions->transition(sessionId, GbMediaStopping, GbMediaFailed,
                                      _clock(), detail, nullptr);
                _zlm->closeRtpServer(session.streamId,
                    [](const ZlmRtpCloseResult &) {});
            }
            return false;
        }
        dialog = found->second;
        ++found->second.nextCseq;
    }
    const SipMessage bye = SipRequestFactory::dialogBye(
        _config, dialog.invite, dialog.acceptedResponse,
        dialog.nextCseq, token("bye", _sequence.fetch_add(1) + 1));
    std::weak_ptr<GbLiveService> weakSelf = shared_from_this();
    return _transactions->send(
        bye, dialog.sender, _config.transactionTimeoutSeconds,
        [weakSelf, sessionId, failureReason](const SipTransactionResult &result) {
            const Ptr self = weakSelf.lock();
            if (self) {
                self->finishBye(sessionId, failureReason, result);
            }
        }, error);
}

void GbLiveService::finishBye(const std::string &sessionId,
                              const std::string &failureReason,
                              const SipTransactionResult &result) {
    std::string detail = failureReason;
    const std::string byeError = transactionFailure(result, "BYE");
    if (!byeError.empty()) {
        if (!detail.empty()) {
            detail += "; ";
        }
        detail += byeError;
    }
    const GbMediaSessionState terminalState =
        detail.empty() ? GbMediaStopped : GbMediaFailed;
    GbMediaSession session;
    if (!_sessions->find(sessionId, session)) {
        return;
    }
    std::weak_ptr<GbLiveService> weakSelf = shared_from_this();
    _zlm->closeRtpServer(session.streamId,
        [weakSelf, sessionId, terminalState, detail](const ZlmRtpCloseResult &closeResult) {
            const Ptr self = weakSelf.lock();
            if (self) {
                self->finishAfterRtpClose(sessionId, terminalState,
                                          detail, closeResult);
            }
        });
}

void GbLiveService::finishAfterRtpClose(const std::string &sessionId,
                                        GbMediaSessionState terminalState,
                                        const std::string &detail,
                                        const ZlmRtpCloseResult &result) {
    GbMediaSessionState finalState = terminalState;
    std::string finalDetail = detail;
    if (!result.ok) {
        finalState = GbMediaFailed;
        if (!finalDetail.empty()) {
            finalDetail += "; ";
        }
        finalDetail += result.error;
    }
    _sessions->transition(sessionId, GbMediaStopping, finalState,
                          _clock(), finalDetail, nullptr);
    std::lock_guard<std::mutex> lock(_mutex);
    _dialogs.erase(sessionId);
}

void GbLiveService::failBeforeDialog(const std::string &sessionId,
                                     GbMediaSessionState expected,
                                     const std::string &detail,
                                     bool closeRtp) {
    GbMediaSession session;
    if (!_sessions->find(sessionId, session)) {
        return;
    }
    _sessions->transition(sessionId, expected, GbMediaFailed,
                          _clock(), detail, nullptr);
    if (closeRtp) {
        _zlm->closeRtpServer(session.streamId,
            [](const ZlmRtpCloseResult &) {});
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _dialogs.erase(sessionId);
}

bool GbLiveService::handleResponse(const SipMessage &response) {
    if (_transactions->handleResponse(response)) {
        return true;
    }
    std::string method;
    if (response.isRequest() || response.statusCode() < 200 ||
        response.statusCode() >= 300 ||
        !parseCseqMethod(response, method) || method != "INVITE") {
        return false;
    }
    GbMediaSession session;
    if (!_sessions->findByCallId(response.header("Call-ID"), session) ||
        (session.state != GbMediaStreaming && session.state != GbMediaStopping)) {
        return false;
    }
    MediaDialog dialog;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::map<std::string, MediaDialog>::const_iterator found =
            _dialogs.find(session.sessionId);
        if (found == _dialogs.end()) {
            return false;
        }
        dialog = found->second;
    }
    const SipMessage ack = SipRequestFactory::inviteAck(
        _config, dialog.invite, response,
        token("ack", _sequence.fetch_add(1) + 1));
    if (dialog.sender) {
        dialog.sender(ack.serialize());
    }
    return true;
}

void GbLiveService::sweep() {
    _transactions->expire();
}

bool GbLiveService::findSession(const std::string &sessionId,
                                GbMediaSession &session) const {
    return _sessions->find(sessionId, session);
}

std::vector<GbMediaSession> GbLiveService::listSessions() const {
    return _sessions->list();
}

size_t GbLiveService::pendingTransactions() const {
    return _transactions->size();
}

} // namespace gb28181
} // namespace easy_sva
