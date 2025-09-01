#include <openssl/sha.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include "util.hpp"

std::string sha1(const std::vector<uint8_t>& data) {
    unsigned char hash[SHA_DIGEST_LENGTH]; // 20字节
    SHA1(reinterpret_cast<const unsigned char*>(data.data()),
         data.size(),
         hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    }
    return oss.str();
}
