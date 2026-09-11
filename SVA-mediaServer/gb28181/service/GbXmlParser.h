#ifndef EASY_SVA_GB28181_GB_XML_PARSER_H
#define EASY_SVA_GB28181_GB_XML_PARSER_H

#include <cstddef>
#include <string>
#include <vector>

namespace easy_sva {
namespace gb28181 {

struct GbCatalogItem {
    std::string deviceId;
    std::string name;
    std::string manufacturer;
    std::string model;
    std::string owner;
    std::string civilCode;
    std::string address;
    std::string parental;
    std::string parentId;
    std::string safetyWay;
    std::string registerWay;
    std::string secrecy;
    std::string status;
    std::string longitude;
    std::string latitude;
};

struct GbXmlMessage {
    std::string rootName;
    std::string command;
    std::string serialNumber;
    std::string deviceId;
    std::string status;
    bool hasSumNum;
    size_t sumNum;
    std::vector<GbCatalogItem> catalogItems;

    GbXmlMessage();
};

class GbXmlParser {
public:
    static bool parse(const std::string &body,
                      GbXmlMessage &message,
                      std::string *error = nullptr);
};

} // namespace gb28181
} // namespace easy_sva

#endif // EASY_SVA_GB28181_GB_XML_PARSER_H
