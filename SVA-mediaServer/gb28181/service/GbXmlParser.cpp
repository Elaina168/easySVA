#include "GbXmlParser.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>

#if defined(__linux__) || defined(__APPLE__)
#include <iconv.h>
#endif

#include "Onvif/pugixml.hpp"

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

std::string trim(const std::string &value) {
    size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' ||
            value[begin] == '\r' || value[begin] == '\n')) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin &&
           (value[end - 1] == ' ' || value[end - 1] == '\t' ||
            value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string localName(const char *name) {
    const std::string full = name ? name : "";
    const size_t separator = full.rfind(':');
    return separator == std::string::npos ? full : full.substr(separator + 1);
}

pugi::xml_node childNamed(const pugi::xml_node &parent, const std::string &name) {
    const std::string wanted = lowerAscii(name);
    for (pugi::xml_node child = parent.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_element && lowerAscii(localName(child.name())) == wanted) {
            return child;
        }
    }
    return pugi::xml_node();
}

std::string childText(const pugi::xml_node &parent, const std::string &name) {
    const pugi::xml_node child = childNamed(parent, name);
    return child ? trim(child.text().as_string()) : std::string();
}

bool parseSize(const std::string &value, size_t &parsed) {
    if (value.empty() || value[0] == '-') {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const unsigned long long number = std::strtoull(value.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != '\0' ||
        number > std::numeric_limits<size_t>::max()) {
        return false;
    }
    parsed = static_cast<size_t>(number);
    return true;
}

bool declaresGbEncoding(const std::string &body) {
    const std::string prefix = lowerAscii(body.substr(0, std::min<size_t>(body.size(), 256)));
    const size_t encoding = prefix.find("encoding");
    if (encoding == std::string::npos) {
        return false;
    }
    const size_t gb2312 = prefix.find("gb2312", encoding);
    const size_t gbk = prefix.find("gbk", encoding);
    const size_t gb18030 = prefix.find("gb18030", encoding);
    return gb2312 != std::string::npos || gbk != std::string::npos || gb18030 != std::string::npos;
}

bool convertGb18030ToUtf8(const std::string &input,
                          std::string &output,
                          std::string *error) {
#if defined(__linux__) || defined(__APPLE__)
    iconv_t converter = iconv_open("UTF-8", "GB18030");
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        setError(error, "cannot create GB18030-to-UTF-8 converter");
        return false;
    }

    std::string converted(input.size() * 3 + 16, '\0');
    char *inputCursor = const_cast<char *>(input.data());
    size_t inputRemaining = input.size();
    char *outputCursor = &converted[0];
    size_t outputRemaining = converted.size();
    while (inputRemaining != 0) {
        if (iconv(converter, &inputCursor, &inputRemaining,
                  &outputCursor, &outputRemaining) != static_cast<size_t>(-1)) {
            continue;
        }
        if (errno != E2BIG) {
            iconv_close(converter);
            setError(error, "invalid GB18030 XML body");
            return false;
        }
        const size_t used = converted.size() - outputRemaining;
        converted.resize(converted.size() * 2);
        outputCursor = &converted[0] + used;
        outputRemaining = converted.size() - used;
    }
    iconv_close(converter);
    converted.resize(converted.size() - outputRemaining);
    output.swap(converted);
    return true;
#else
    (void)input;
    (void)output;
    setError(error, "GB2312/GBK XML conversion is unavailable on this platform");
    return false;
#endif
}

GbCatalogItem parseCatalogItem(const pugi::xml_node &item) {
    GbCatalogItem parsed;
    parsed.deviceId = childText(item, "DeviceID");
    parsed.name = childText(item, "Name");
    parsed.manufacturer = childText(item, "Manufacturer");
    parsed.model = childText(item, "Model");
    parsed.owner = childText(item, "Owner");
    parsed.civilCode = childText(item, "CivilCode");
    parsed.address = childText(item, "Address");
    parsed.parental = childText(item, "Parental");
    parsed.parentId = childText(item, "ParentID");
    parsed.safetyWay = childText(item, "SafetyWay");
    parsed.registerWay = childText(item, "RegisterWay");
    parsed.secrecy = childText(item, "Secrecy");
    parsed.status = childText(item, "Status");
    parsed.longitude = childText(item, "Longitude");
    parsed.latitude = childText(item, "Latitude");
    return parsed;
}

} // namespace

GbXmlMessage::GbXmlMessage() : hasSumNum(false), sumNum(0) {}

bool GbXmlParser::parse(const std::string &body,
                        GbXmlMessage &message,
                        std::string *error) {
    if (error) {
        error->clear();
    }
    if (body.empty()) {
        setError(error, "GB28181 XML body is empty");
        return false;
    }

    std::string xml = body;
    if (declaresGbEncoding(body) && !convertGb18030ToUtf8(body, xml, error)) {
        return false;
    }

    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_buffer(
        xml.data(), xml.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!result) {
        setError(error, std::string("invalid GB28181 XML: ") + result.description());
        return false;
    }

    const pugi::xml_node root = document.document_element();
    if (!root) {
        setError(error, "GB28181 XML has no root element");
        return false;
    }

    GbXmlMessage parsed;
    parsed.rootName = localName(root.name());
    parsed.command = childText(root, "CmdType");
    parsed.serialNumber = childText(root, "SN");
    parsed.deviceId = childText(root, "DeviceID");
    parsed.status = childText(root, "Status");
    if (parsed.command.empty() || parsed.deviceId.empty()) {
        setError(error, "GB28181 XML requires CmdType and DeviceID");
        return false;
    }

    const std::string sumNum = childText(root, "SumNum");
    if (!sumNum.empty()) {
        parsed.hasSumNum = true;
        if (!parseSize(sumNum, parsed.sumNum)) {
            setError(error, "invalid Catalog SumNum");
            return false;
        }
    }

    const pugi::xml_node deviceList = childNamed(root, "DeviceList");
    if (deviceList) {
        for (pugi::xml_node item = deviceList.first_child(); item; item = item.next_sibling()) {
            if (item.type() == pugi::node_element &&
                lowerAscii(localName(item.name())) == "item") {
                parsed.catalogItems.push_back(parseCatalogItem(item));
            }
        }
    }
    if (parsed.hasSumNum && parsed.sumNum < parsed.catalogItems.size()) {
        setError(error, "Catalog SumNum is smaller than the supplied item count");
        return false;
    }

    message = parsed;
    return true;
}

} // namespace gb28181
} // namespace easy_sva
