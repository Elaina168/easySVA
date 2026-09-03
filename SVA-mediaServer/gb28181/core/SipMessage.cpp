#include "SipMessage.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace easy_sva {
namespace gb28181 {
namespace {

std::string trim(const std::string &value) {
    const std::string whitespace = " \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return std::string();
    }
    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
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

std::string canonicalHeaderName(const std::string &name) {
    std::string lowered = lowerAscii(trim(name));
    if (lowered.size() != 1) {
        return lowered;
    }

    switch (lowered[0]) {
    case 'v': return "via";
    case 'f': return "from";
    case 't': return "to";
    case 'i': return "call-id";
    case 'm': return "contact";
    case 'l': return "content-length";
    case 'c': return "content-type";
    case 's': return "subject";
    case 'k': return "supported";
    default: return lowered;
    }
}

bool sameHeaderName(const std::string &left, const std::string &right) {
    return canonicalHeaderName(left) == canonicalHeaderName(right);
}

void setError(std::string *error, const std::string &value) {
    if (error) {
        *error = value;
    }
}

bool parseUnsigned(const std::string &text, size_t &value) {
    const std::string cleaned = trim(text);
    if (cleaned.empty() || cleaned[0] == '-') {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(cleaned.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != '\0' ||
        parsed > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        return false;
    }
    value = static_cast<size_t>(parsed);
    return true;
}

bool parseStartLine(const std::string &line, SipMessage &message, std::string *error) {
    std::istringstream input(line);
    std::string first;
    if (!(input >> first)) {
        setError(error, "empty SIP start line");
        return false;
    }

    if (first.compare(0, 4, "SIP/") == 0) {
        int statusCode = 0;
        if (!(input >> statusCode) || statusCode < 100 || statusCode > 699) {
            setError(error, "invalid SIP status code");
            return false;
        }
        std::string reason;
        std::getline(input, reason);
        message.setStatusLine(statusCode, trim(reason));
        if (first != "SIP/2.0") {
            setError(error, "unsupported SIP version");
            return false;
        }
        return true;
    }

    std::string uri;
    std::string version;
    std::string trailing;
    if (!(input >> uri >> version) || (input >> trailing)) {
        setError(error, "invalid SIP request line");
        return false;
    }
    if (version != "SIP/2.0") {
        setError(error, "unsupported SIP version");
        return false;
    }
    message.setRequestLine(first, uri);
    return true;
}

} // namespace

SipMessage::SipMessage()
    : _is_request(true), _status_code(0), _version("SIP/2.0") {}

bool SipMessage::parse(const std::string &wire,
                       SipMessage &message,
                       size_t *consumed,
                       std::string *error) {
    if (consumed) {
        *consumed = 0;
    }
    if (error) {
        error->clear();
    }

    size_t delimiterSize = 4;
    size_t headerEnd = wire.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        delimiterSize = 2;
        headerEnd = wire.find("\n\n");
    }
    if (headerEnd == std::string::npos) {
        setError(error, "incomplete SIP headers");
        return false;
    }

    SipMessage parsed;
    const std::string headerBlock = wire.substr(0, headerEnd);
    size_t cursor = 0;
    size_t lineEnd = headerBlock.find('\n');
    std::string startLine = headerBlock.substr(0, lineEnd);
    if (!startLine.empty() && startLine[startLine.size() - 1] == '\r') {
        startLine.resize(startLine.size() - 1);
    }
    if (!parseStartLine(startLine, parsed, error)) {
        return false;
    }

    cursor = lineEnd == std::string::npos ? headerBlock.size() : lineEnd + 1;
    while (cursor < headerBlock.size()) {
        lineEnd = headerBlock.find('\n', cursor);
        const size_t length = lineEnd == std::string::npos
            ? headerBlock.size() - cursor
            : lineEnd - cursor;
        std::string line = headerBlock.substr(cursor, length);
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.resize(line.size() - 1);
        }

        if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            if (parsed._headers.empty()) {
                setError(error, "SIP header continuation has no previous header");
                return false;
            }
            parsed._headers.back().value += " " + trim(line);
        } else {
            const size_t colon = line.find(':');
            if (colon == std::string::npos) {
                setError(error, "invalid SIP header line");
                return false;
            }
            const std::string name = trim(line.substr(0, colon));
            if (name.empty()) {
                setError(error, "empty SIP header name");
                return false;
            }
            parsed.addHeader(name, trim(line.substr(colon + 1)));
        }

        if (lineEnd == std::string::npos) {
            break;
        }
        cursor = lineEnd + 1;
    }

    const size_t bodyStart = headerEnd + delimiterSize;
    size_t bodyLength = 0;
    const std::vector<std::string> lengthHeaders = parsed.headerValues("Content-Length");
    if (!lengthHeaders.empty()) {
        size_t declaredLength = 0;
        if (!parseUnsigned(lengthHeaders.front(), declaredLength)) {
            setError(error, "invalid SIP Content-Length");
            return false;
        }
        for (size_t index = 1; index < lengthHeaders.size(); ++index) {
            size_t duplicateLength = 0;
            if (!parseUnsigned(lengthHeaders[index], duplicateLength) ||
                duplicateLength != declaredLength) {
                setError(error, "conflicting SIP Content-Length headers");
                return false;
            }
        }
        if (wire.size() - bodyStart < declaredLength) {
            setError(error, "incomplete SIP body");
            return false;
        }
        bodyLength = declaredLength;
    }

    parsed._body = wire.substr(bodyStart, bodyLength);
    message = parsed;
    if (consumed) {
        *consumed = bodyStart + bodyLength;
    }
    return true;
}

bool SipMessage::isRequest() const { return _is_request; }
const std::string &SipMessage::method() const { return _method; }
const std::string &SipMessage::requestUri() const { return _request_uri; }
int SipMessage::statusCode() const { return _status_code; }
const std::string &SipMessage::reasonPhrase() const { return _reason_phrase; }
const std::string &SipMessage::version() const { return _version; }

void SipMessage::setRequestLine(const std::string &method, const std::string &uri) {
    _is_request = true;
    _method = method;
    _request_uri = uri;
    _status_code = 0;
    _reason_phrase.clear();
    _version = "SIP/2.0";
}

void SipMessage::setStatusLine(int statusCode, const std::string &reasonPhrase) {
    _is_request = false;
    _method.clear();
    _request_uri.clear();
    _status_code = statusCode;
    _reason_phrase = reasonPhrase;
    _version = "SIP/2.0";
}

const std::vector<SipHeader> &SipMessage::headers() const { return _headers; }

std::vector<std::string> SipMessage::headerValues(const std::string &name) const {
    std::vector<std::string> values;
    for (std::vector<SipHeader>::const_iterator it = _headers.begin(); it != _headers.end(); ++it) {
        if (sameHeaderName(it->name, name)) {
            values.push_back(it->value);
        }
    }
    return values;
}

std::string SipMessage::header(const std::string &name) const {
    for (std::vector<SipHeader>::const_iterator it = _headers.begin(); it != _headers.end(); ++it) {
        if (sameHeaderName(it->name, name)) {
            return it->value;
        }
    }
    return std::string();
}

bool SipMessage::hasHeader(const std::string &name) const {
    for (std::vector<SipHeader>::const_iterator it = _headers.begin(); it != _headers.end(); ++it) {
        if (sameHeaderName(it->name, name)) {
            return true;
        }
    }
    return false;
}

void SipMessage::addHeader(const std::string &name, const std::string &value) {
    SipHeader headerValue;
    headerValue.name = name;
    headerValue.value = value;
    _headers.push_back(headerValue);
}

void SipMessage::setHeader(const std::string &name, const std::string &value) {
    bool replaced = false;
    std::vector<SipHeader> updated;
    updated.reserve(_headers.size() + 1);
    for (std::vector<SipHeader>::const_iterator it = _headers.begin(); it != _headers.end(); ++it) {
        if (sameHeaderName(it->name, name)) {
            if (!replaced) {
                SipHeader replacement;
                replacement.name = name;
                replacement.value = value;
                updated.push_back(replacement);
                replaced = true;
            }
        } else {
            updated.push_back(*it);
        }
    }
    if (!replaced) {
        addHeader(name, value);
        return;
    }
    _headers.swap(updated);
}

const std::string &SipMessage::body() const { return _body; }
void SipMessage::setBody(const std::string &body) { _body = body; }

std::string SipMessage::serialize() const {
    std::ostringstream output;
    if (_is_request) {
        output << _method << ' ' << _request_uri << ' ' << _version << "\r\n";
    } else {
        output << _version << ' ' << _status_code << ' ' << _reason_phrase << "\r\n";
    }
    for (std::vector<SipHeader>::const_iterator it = _headers.begin(); it != _headers.end(); ++it) {
        if (!sameHeaderName(it->name, "Content-Length")) {
            output << it->name << ": " << it->value << "\r\n";
        }
    }
    output << "Content-Length: " << _body.size() << "\r\n\r\n" << _body;
    return output.str();
}

} // namespace gb28181
} // namespace easy_sva
