#include "DigestAuth.h"

#include <algorithm>
#include <cctype>

#include "Util/MD5.h"

namespace easy_sva {
namespace gb28181 {
namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return ch;
    });
    return value;
}

void setError(std::string *error, const std::string &value) {
    if (error) {
        *error = value;
    }
}

void skipSeparators(const std::string &text, size_t &cursor) {
    while (cursor < text.size() &&
           (text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == ',')) {
        ++cursor;
    }
}

std::string escapeQuoted(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (*it == '\\' || *it == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(*it);
    }
    return escaped;
}

bool constantTimeEquals(const std::string &left, const std::string &right) {
    if (left.size() != right.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t index = 0; index < left.size(); ++index) {
        difference |= static_cast<unsigned char>(left[index] ^ right[index]);
    }
    return difference == 0;
}

std::string md5(const std::string &value) {
    return toolkit::MD5(value).hexdigest();
}

bool readRequired(const std::map<std::string, std::string> &values,
                  const std::string &name,
                  std::string &target,
                  std::string *error) {
    const std::map<std::string, std::string>::const_iterator it = values.find(name);
    if (it == values.end() || it->second.empty()) {
        setError(error, "Digest credential is missing " + name);
        return false;
    }
    target = it->second;
    return true;
}

} // namespace

bool DigestAuth::parseCredentials(const std::string &authorization,
                                  DigestCredentials &credentials,
                                  std::string *error) {
    if (error) {
        error->clear();
    }
    credentials = DigestCredentials();

    size_t cursor = 0;
    while (cursor < authorization.size() &&
           (authorization[cursor] == ' ' || authorization[cursor] == '\t')) {
        ++cursor;
    }
    const size_t schemeStart = cursor;
    while (cursor < authorization.size() &&
           authorization[cursor] != ' ' && authorization[cursor] != '\t') {
        ++cursor;
    }
    if (lowerAscii(authorization.substr(schemeStart, cursor - schemeStart)) != "digest") {
        setError(error, "Authorization scheme is not Digest");
        return false;
    }

    std::map<std::string, std::string> values;
    while (cursor < authorization.size()) {
        skipSeparators(authorization, cursor);
        if (cursor >= authorization.size()) {
            break;
        }

        const size_t nameStart = cursor;
        while (cursor < authorization.size() && authorization[cursor] != '=' &&
               authorization[cursor] != ',' && authorization[cursor] != ' ' &&
               authorization[cursor] != '\t') {
            ++cursor;
        }
        const std::string name = lowerAscii(authorization.substr(nameStart, cursor - nameStart));
        while (cursor < authorization.size() &&
               (authorization[cursor] == ' ' || authorization[cursor] == '\t')) {
            ++cursor;
        }
        if (name.empty() || cursor >= authorization.size() || authorization[cursor] != '=') {
            setError(error, "invalid Digest credential parameter");
            return false;
        }
        ++cursor;
        while (cursor < authorization.size() &&
               (authorization[cursor] == ' ' || authorization[cursor] == '\t')) {
            ++cursor;
        }

        std::string value;
        if (cursor < authorization.size() && authorization[cursor] == '"') {
            ++cursor;
            bool closed = false;
            while (cursor < authorization.size()) {
                const char ch = authorization[cursor++];
                if (ch == '\\' && cursor < authorization.size()) {
                    value.push_back(authorization[cursor++]);
                } else if (ch == '"') {
                    closed = true;
                    break;
                } else {
                    value.push_back(ch);
                }
            }
            if (!closed) {
                setError(error, "unterminated quoted Digest value");
                return false;
            }
        } else {
            const size_t valueStart = cursor;
            while (cursor < authorization.size() && authorization[cursor] != ',') {
                ++cursor;
            }
            size_t valueEnd = cursor;
            while (valueEnd > valueStart &&
                   (authorization[valueEnd - 1] == ' ' || authorization[valueEnd - 1] == '\t')) {
                --valueEnd;
            }
            value = authorization.substr(valueStart, valueEnd - valueStart);
        }
        values[name] = value;

        while (cursor < authorization.size() &&
               (authorization[cursor] == ' ' || authorization[cursor] == '\t')) {
            ++cursor;
        }
        if (cursor < authorization.size() && authorization[cursor] != ',') {
            setError(error, "Digest parameters must be comma separated");
            return false;
        }
    }

    if (!readRequired(values, "username", credentials.username, error) ||
        !readRequired(values, "realm", credentials.realm, error) ||
        !readRequired(values, "nonce", credentials.nonce, error) ||
        !readRequired(values, "uri", credentials.uri, error) ||
        !readRequired(values, "response", credentials.response, error)) {
        return false;
    }

    credentials.algorithm = values["algorithm"];
    credentials.qop = lowerAscii(values["qop"]);
    credentials.nonceCount = values["nc"];
    credentials.clientNonce = values["cnonce"];
    if (!credentials.algorithm.empty() && lowerAscii(credentials.algorithm) != "md5") {
        setError(error, "unsupported Digest algorithm");
        return false;
    }
    if (!credentials.qop.empty() && credentials.qop != "auth") {
        setError(error, "unsupported Digest qop");
        return false;
    }
    if (!credentials.qop.empty() &&
        (credentials.nonceCount.empty() || credentials.clientNonce.empty())) {
        setError(error, "Digest qop requires nc and cnonce");
        return false;
    }
    return true;
}

std::string DigestAuth::computeResponse(const std::string &method,
                                        const std::string &password,
                                        const DigestCredentials &credentials) {
    if (method.empty() || credentials.username.empty() || credentials.realm.empty() ||
        credentials.nonce.empty() || credentials.uri.empty()) {
        return std::string();
    }
    if (!credentials.algorithm.empty() && lowerAscii(credentials.algorithm) != "md5") {
        return std::string();
    }

    const std::string ha1 = md5(credentials.username + ":" + credentials.realm + ":" + password);
    const std::string ha2 = md5(method + ":" + credentials.uri);
    if (credentials.qop.empty()) {
        return md5(ha1 + ":" + credentials.nonce + ":" + ha2);
    }
    if (lowerAscii(credentials.qop) != "auth" || credentials.nonceCount.empty() ||
        credentials.clientNonce.empty()) {
        return std::string();
    }
    return md5(ha1 + ":" + credentials.nonce + ":" + credentials.nonceCount + ":" +
               credentials.clientNonce + ":auth:" + ha2);
}

bool DigestAuth::verifyRequest(const SipMessage &request,
                               const std::string &password,
                               const std::string &expectedRealm,
                               const std::string &expectedNonce,
                               std::string *error) {
    if (error) {
        error->clear();
    }
    if (!request.isRequest()) {
        setError(error, "Digest verification requires a SIP request");
        return false;
    }

    std::string authorization = request.header("Authorization");
    if (authorization.empty()) {
        authorization = request.header("Proxy-Authorization");
    }
    if (authorization.empty()) {
        setError(error, "SIP request has no Digest authorization");
        return false;
    }

    DigestCredentials credentials;
    if (!parseCredentials(authorization, credentials, error)) {
        return false;
    }
    if (credentials.realm != expectedRealm || credentials.nonce != expectedNonce) {
        setError(error, "Digest realm or nonce does not match the challenge");
        return false;
    }
    if (credentials.uri != request.requestUri()) {
        setError(error, "Digest URI does not match the SIP request URI");
        return false;
    }

    const std::string expected = computeResponse(request.method(), password, credentials);
    if (expected.empty() || !constantTimeEquals(lowerAscii(credentials.response), expected)) {
        setError(error, "Digest response is invalid");
        return false;
    }
    return true;
}

std::string DigestAuth::buildChallenge(const std::string &realm,
                                       const std::string &nonce,
                                       bool stale) {
    std::string challenge = "Digest realm=\"" + escapeQuoted(realm) +
        "\", nonce=\"" + escapeQuoted(nonce) + "\", algorithm=MD5, qop=\"auth\"";
    if (stale) {
        challenge += ", stale=true";
    }
    return challenge;
}

} // namespace gb28181
} // namespace easy_sva
