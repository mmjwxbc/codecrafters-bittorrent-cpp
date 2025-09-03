#ifndef UTIL_HPP
#define UTIL_HPP
#include <string>
#include "lib/nlohmann/json.hpp"
#include <vector>
using json = nlohmann::json;

struct Piece {
    unsigned piece_index;
    unsigned begin_index;
    unsigned length;
    std::string data;
};

std::string sha1(const std::vector<uint8_t>& data);
int connect(const std::string ip, const std::string port);
int handle_handshake(const std::string ip, const uint16_t port, const std::string info);
int download_block(const int sockfd, const unsigned piece_index, const unsigned begin_index, const unsigned length);
int handle_wave(const int sockfd);
int handle_peers(const json &torrent, std::vector<std::string> &ips, std::vector<uint16_t> &ports);
json decode_bencoded_value(const std::string &encoded_value, size_t &begin);
std::string encode_bencode_value(const json& value);
struct Piece wait_block(const int sockfd);
int write_to_file(char *filename, std::vector<struct Piece> &piece);
std::map<std::string, std::string> parse_magnet(const std::string& magnet);
int handle_magnet_peers(const std::string announce_url, const std::string hash, std::vector<std::string> &ips, std::vector<uint16_t> &ports);
int handle_magnet_handshake(const std::string ip, const uint16_t port, const std::string hash, unsigned char &metadata_id);
json handle_magnet_info(const int sockfd, unsigned char metadata_id);
#endif