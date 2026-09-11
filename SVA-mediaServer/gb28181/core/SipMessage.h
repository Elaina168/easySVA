#ifndef EASY_SVA_GB28181_SIP_MESSAGE_H
#define EASY_SVA_GB28181_SIP_MESSAGE_H

#include <cstddef>
#include <string>
#include <vector>

namespace easy_sva {
namespace gb28181 {

struct SipHeader {
    std::string name;
    std::string value;
};

class SipMessage {
public:
    SipMessage();

    static bool parse(const std::string &wire,
                      SipMessage &message,
                      size_t *consumed = nullptr,
                      std::string *error = nullptr);

    bool isRequest() const;
    const std::string &method() const;
    const std::string &requestUri() const;
    int statusCode() const;
    const std::string &reasonPhrase() const;
    const std::string &version() const;

    void setRequestLine(const std::string &method, const std::string &uri);
    void setStatusLine(int statusCode, const std::string &reasonPhrase);

    const std::vector<SipHeader> &headers() const;
    std::vector<std::string> headerValues(const std::string &name) const;
    std::string header(const std::string &name) const;
    bool hasHeader(const std::string &name) const;
    void addHeader(const std::string &name, const std::string &value);
    void setHeader(const std::string &name, const std::string &value);

    const std::string &body() const;
    void setBody(const std::string &body);
    std::string serialize() const;

private:
    bool _is_request;
    std::string _method;
    std::string _request_uri;
    int _status_code;
    std::string _reason_phrase;
    std::string _version;
    std::vector<SipHeader> _headers;
    std::string _body;
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_SIP_MESSAGE_H
