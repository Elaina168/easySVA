#include "SipStreamDecoder.h"

namespace easy_sva {
namespace gb28181 {
namespace {

void setError(std::string *error, const std::string &value) {
    if (error) {
        *error = value;
    }
}

bool isIncomplete(const std::string &error) {
    return error.compare(0, 10, "incomplete") == 0;
}

} // namespace

SipStreamDecoder::SipStreamDecoder(size_t maxMessageBytes)
    : _max_message_bytes(maxMessageBytes) {}

bool SipStreamDecoder::append(const char *data, size_t size, std::string *error) {
    if (error) {
        error->clear();
    }
    if (!data && size != 0) {
        setError(error, "SIP stream data is null");
        return false;
    }
    if (size > _max_message_bytes || _buffer.size() > _max_message_bytes - size) {
        setError(error, "SIP stream exceeds the configured message limit");
        return false;
    }
    _buffer.append(data, size);
    return true;
}

bool SipStreamDecoder::append(const std::string &data, std::string *error) {
    return append(data.data(), data.size(), error);
}

SipStreamDecoder::Result SipStreamDecoder::next(SipMessage &message, std::string *error) {
    if (error) {
        error->clear();
    }
    if (_buffer.empty()) {
        return NeedMoreData;
    }

    size_t consumed = 0;
    std::string parseError;
    if (!SipMessage::parse(_buffer, message, &consumed, &parseError)) {
        if (isIncomplete(parseError)) {
            return NeedMoreData;
        }
        setError(error, parseError);
        return InvalidMessage;
    }
    if (consumed == 0 || consumed > _max_message_bytes) {
        setError(error, "SIP message exceeds the configured message limit");
        return InvalidMessage;
    }
    _buffer.erase(0, consumed);
    return MessageReady;
}

void SipStreamDecoder::clear() {
    _buffer.clear();
}

size_t SipStreamDecoder::bufferedBytes() const {
    return _buffer.size();
}

} // namespace gb28181
} // namespace easy_sva
