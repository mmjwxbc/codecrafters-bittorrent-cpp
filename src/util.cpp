#include <cstdlib>
#include <openssl/sha.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include "util.hpp"
#include <arpa/inet.h>   // sockaddr_in, inet_pton
#include <netinet/in.h>  // sockaddr_in
#include <sys/socket.h>  // socket, connect
#include <unistd.h>      // close
#include <cstring>

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


int connect(const std::string ip, const std::string port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        return -1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port.c_str()));  // 注意大端序转换
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        return -1;
    }
    return sockfd;
}