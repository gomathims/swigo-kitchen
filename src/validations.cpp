#include "validations.h"
#include <regex>

// Function to validate an IPv4 address
bool isValidIPv4(const String& ipAddress) {
    // Regular expression for IPv4 validation
    const std::regex ipv4Pattern(
        R"(^((25[0-5]|2[0-4][0-9]|[0-1]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9][0-9]?)$)");

    // Convert String to std::string for regex compatibility
    std::string ip = ipAddress.c_str();

    // Validate the IP address using regex
    return std::regex_match(ip, ipv4Pattern);
}