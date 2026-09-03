#ifndef EASY_SVA_GB28181_SIP_STREAM_DECODER_H
#define EASY_SVA_GB28181_SIP_STREAM_DECODER_H

#include <cstddef>
#include <string>

#include "SipMessage.h"

namespace easy_sva {
namespace gb28181 {

class SipStreamDecoder {
public:
    enum Result {
        NeedMoreData,
        MessageReady,
        InvalidMessage
    };

    explicit SipStreamDecoder(size_t maxMessageBytes);

    bool append(const char *data, size_t size, std::string *error = nullptr);
    bool append(const std::string &data, std::string *error = nullptr);

    Result next(SipMessage &message, std::string *error = nullptr);
    void clear();
    size_t bufferedBytes() const;

private:
    size_t _max_message_bytes;
    std::string _buffer;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_SIP_STREAM_DECODER_H
