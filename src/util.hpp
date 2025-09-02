#ifndef UTIL_HPP
#define UTIL_HPP

#include <string>
#include <vector>

std::string sha1(const std::vector<uint8_t>& data);
int connect(const std::string ip, const std::string port);
#endif