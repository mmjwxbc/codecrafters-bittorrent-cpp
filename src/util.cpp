#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <openssl/sha.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <vector>
#include "util.hpp"
#include <arpa/inet.h>   // sockaddr_in, inet_pton
#include <netinet/in.h>  // sockaddr_in
#include <sys/socket.h>  // socket, connect
#include <unistd.h>      // close
#include <cstring>
#include <curl/curl.h>
#include <fstream>
using namespace std;

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    std::string *response = reinterpret_cast<std::string*>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

json decode_bencoded_value(const string &encoded_value, size_t &begin) {
  if (isdigit(encoded_value[begin])) {
    // Example: "5:hello" -> "hello"
    size_t colon_index = encoded_value.find(':', begin);
    if (colon_index != string::npos) {
      string number_string =
          encoded_value.substr(begin, colon_index - begin);
      int64_t number = atoll(number_string.c_str());
      string str = encoded_value.substr(colon_index + 1, number);
      begin = colon_index + number + 1;
      return json(str);
    } else {
      throw runtime_error("Invalid encoded value: " + encoded_value);
    }
  } else if (encoded_value[begin] == 'i') {
    size_t e_index = encoded_value.find('e', begin);
    int64_t number = atoll(
        encoded_value.substr(begin + 1, e_index - begin - 1).c_str());
    begin = e_index + 1;
    return json(number);
  } else if (encoded_value[begin] == 'l') {
    json array = json::array();
    begin++;
    while (encoded_value[begin] != 'e') {
      json value = decode_bencoded_value(encoded_value, begin);
      array.push_back(value);
    }
    begin++;
    return array;
  } else if (encoded_value[begin] == 'd') {
    json object = json::object();
    begin++;
    while (encoded_value[begin] != 'e') {
      json key = decode_bencoded_value(encoded_value, begin);
      json value = decode_bencoded_value(encoded_value, begin);
      object[key] = value;
    }
    begin++;
    return object;
  } else {
    throw runtime_error("Unhandled encoded value: " + encoded_value);
  }
}

ssize_t read_nbytes(int sockfd, vector<uint8_t> &recv_buf, size_t len) {
    static size_t cnt = 0;
    size_t total = recv_buf.size();
    uint8_t tmp[65534];
    while (total < len) {
        ssize_t n = recv(sockfd, tmp, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        } else if (n == 0) {
            break;
        }
        recv_buf.insert(recv_buf.end(), tmp, tmp + n);
        total += n;
    }
    cnt += total;
    return total;
}

string sha1(const vector<uint8_t>& data) {
    unsigned char hash[SHA_DIGEST_LENGTH]; // 20字节
    SHA1(reinterpret_cast<const unsigned char*>(data.data()),
         data.size(),
         hash);

    ostringstream oss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        oss << hex << setw(2) << setfill('0')
            << static_cast<int>(hash[i]);
    }
    return oss.str();
}

int handle_handshake(const string ip, const uint16_t port, const string info_value) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        return -1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);  // 注意大端序转换
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        return -1;
    }
    vector<uint8_t> bytes(info_value.begin(), info_value.end());
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(bytes.data()),
         bytes.size(),
         hash);
    if(sockfd < 0) return -1;
    char send_data[68] = {0};
    memset(send_data, 0, sizeof(send_data));
    send_data[0] = 19;
    memcpy(send_data + 1,  "BitTorrent protocol", 19);   // 协议名是 ASCII，可以 memcpy
    memcpy(send_data + 28, hash, SHA_DIGEST_LENGTH);     // 二进制：必须 memcpy
    memcpy(send_data + 48, "abcdefghijklmnoptrst", 20);  // 即便是 ASCII，用 memcpy 更直观
    send(sockfd, send_data, 68, 0);
    vector<uint8_t> recv_buf;
    ssize_t n = read_nbytes(sockfd, recv_buf, 68);
    printf("Peer ID: ");
    for(ssize_t i = 0; i < 20 && i + 48 < n; i++) {
      printf("%02x", static_cast<unsigned char>(recv_buf[48 + i]));
    }
    recv_buf.erase(recv_buf.begin(), recv_buf.begin() + 68);
    printf("\n");
    for(ssize_t i = 0; i < 20; i++) {
      printf("%02x", static_cast<unsigned char>(hash[i]));
    }

    cout << "before recv bitfied = " << recv_buf.size() << endl;
    // recv bitfield message
    unsigned prefix_len = 0;
    unsigned char id = 0;
    n = read_nbytes(sockfd, recv_buf, 5);
    cout << "after recv bitfied = " << recv_buf.size() << endl;
    memcpy(&prefix_len, recv_buf.data(), 4);
    prefix_len = ntohl(prefix_len);
    recv_buf.erase(recv_buf.begin(), recv_buf.begin() + 5);
    if(prefix_len > 1) {
      read_nbytes(sockfd, recv_buf, prefix_len - 1);
      recv_buf.erase(recv_buf.begin(), recv_buf.begin() + prefix_len - 1);
    }

    // send interest message
    uint32_t msg_len = htonl(1); // length prefix = 1 (ID only)
    memcpy(send_data, &msg_len, 4);   // 前四字节 = length
    send_data[4] = 2;                 // message ID = 2 (interest)
    send(sockfd, send_data, 5, 0);

    cout << "before recv unchoke = " << recv_buf.size() << endl;
    // recv unchoke message
    n = read_nbytes(sockfd, recv_buf, 5);
    cout << "before after unchoke = " << recv_buf.size() << endl;
    memcpy(&prefix_len, recv_buf.data(), 4);
    prefix_len = ntohl(prefix_len);
    cout << "unchoke message length = " << prefix_len << endl;
    recv_buf.erase(recv_buf.begin(), recv_buf.begin() + 5);
    if(prefix_len > 1) {
      read_nbytes(sockfd, recv_buf, prefix_len - 1);
    }
    return sockfd;
}


int handle_wave(const int sockfd) {
    return close(sockfd);
}

int download_block(const int sockfd, const unsigned piece_index, const unsigned begin_index, const unsigned length){
    uint8_t send_data[1024];
    uint32_t msg_len = htonl(13);
    uint32_t piece_index_n = htonl(piece_index);
    uint32_t begin_index_n = htonl(begin_index);
    uint32_t length_n = htonl(length); 
    memcpy(send_data, &msg_len, 4);
    send_data[4] = 6;
    memcpy(send_data + 5, &piece_index_n, 4);
    memcpy(send_data + 9, &begin_index_n, 4);
    memcpy(send_data + 13, &length_n, 4);

    if(send(sockfd, send_data, 17, 0) != 17) {
        cout << "fuck send download piece" << endl;
        return -1;
    }
    cout << "download block piece index = " << piece_index << " begin index = " << begin_index << endl;
    return 0;
}

int handle_peers(const json &torrent, vector<string> &ips, vector<uint16_t> &ports) {
    CURL *curl;
    CURLcode result;
    curl = curl_easy_init();
    if(curl == NULL) {
      cerr << "curl_easy_init() failed" << endl;
      return -1;
    }
    string info_value = encode_bencode_value(torrent.at("info"));
    vector<uint8_t> bytes(info_value.begin(), info_value.end());
    string announce_url = torrent.at("announce").get<string>();
    string peer_id = "abcdefghijklmnoptrst";
    int64_t length = torrent.at("info").at("length").get<int64_t>();
    ostringstream oss;
    unsigned char hash[SHA_DIGEST_LENGTH]; // 20字节
    SHA1(reinterpret_cast<const unsigned char*>(bytes.data()),
         bytes.size(),
         hash);
    oss << announce_url
        << "?info_hash=" << curl_easy_escape(curl, reinterpret_cast<char*>(hash), SHA_DIGEST_LENGTH)
        << "&peer_id="   << curl_easy_escape(curl, peer_id.c_str(), peer_id.length())
        << "&port="      << 6881
        << "&uploaded=" << 0
        << "&downloaded=" << 0
        << "&left=" << length
        << "&compact=" << 1;
    string url = oss.str();
    string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    result = curl_easy_perform(curl);
    if(result != CURLE_OK) {
      cerr << "curl_easy_perform() failed" << endl;
      return -1;
    }
    size_t begin = 0;
    json content = decode_bencoded_value(response, begin);
    string peers = content.at("peers").get<string>();
    for(size_t i = 0; i < peers.size(); i+=6) {
      unsigned char ip1 = static_cast<unsigned char>(peers[i]);
      unsigned char ip2 = static_cast<unsigned char>(peers[i + 1]);
      unsigned char ip3 = static_cast<unsigned char>(peers[i + 2]);
      unsigned char ip4 = static_cast<unsigned char>(peers[i + 3]);

      uint16_t port = (static_cast<unsigned char>(peers[i + 4]) << 8) |
                      static_cast<unsigned char>(peers[i + 5]);
      // htons(atoi(port.c_str()));
      ports.emplace_back(port);
      ostringstream oss;
        oss << int(ip1) << "." << int(ip2) << "." << int(ip3) << "." << int(ip4);
      ips.emplace_back(oss.str());

      cout << oss.str() << ":" << port << endl;
    }
    curl_easy_cleanup(curl);
    return 0;
}

string encode_bencode_value(const json& value) {
    json::value_t type = value.type();
    if (type == json::value_t::string) {
        string str = value.get<string>();
        return to_string(str.size()) + ":" + str;
    }
    else if (type == json::value_t::number_integer) {
        return "i" + to_string(value.get<int64_t>()) + "e";
    }
    else if (type == json::value_t::array) {
        string result = "l";
        for (const auto& item : value) {
            result += encode_bencode_value(item);
        }
        return result + "e";
    }
    else if (type == json::value_t::object) {
        string result = "d";
        for (auto it = value.begin(); it != value.end(); ++it) {
            result += encode_bencode_value(it.key()) + encode_bencode_value(it.value());
        }
        return result + "e";
    }

    return "";
}

// struct Piece wait_piece(const int sockfd, const unsigned length) {
//     struct Piece piece;
//     vector<uint8_t> recv_buf;
//     while(recv_buf.size() < 13) {
//         read_nbytes(sockfd, recv_buf, 13);
//     }
//     uint32_t prefix_len;
//     uint8_t id;

//     // recv_buf 至少 13 字节 (4+1+4+4)
//     memcpy(&prefix_len, recv_buf.data(), 4);
//     prefix_len = ntohl(prefix_len);

//     memcpy(&id, recv_buf.data() + 4, 1);

//     memcpy(&piece.piece_index, recv_buf.data() + 5, 4);
//     piece.piece_index = ntohl(piece.piece_index);

//     memcpy(&piece.begin_index, recv_buf.data() + 9, 4);
//     piece.begin_index = ntohl(piece.begin_index);
    
//     while(recv_buf.size() < prefix_len - 9) {
//         read_nbytes(sockfd, recv_buf, length);
//     }
//     piece.data.assign((char*)recv_buf.data() + 13, prefix_len - 9);    
//     return piece;
// }

struct Piece wait_block(const int sockfd) {
    Piece piece;
    static std::vector<uint8_t> buf;

    while(buf.size() < 4) read_nbytes(sockfd, buf, 4);

    uint32_t prefix_len_network;
    memcpy(&prefix_len_network, buf.data(), 4);
    uint32_t prefix_len = ntohl(prefix_len_network);

    while(buf.size() < 4 + prefix_len) read_nbytes(sockfd, buf, 4 + prefix_len);

    if (prefix_len < 1) {
      cout << "invalid message length" << endl;
      exit(-1);
    }
    uint8_t id = buf[4];
    if (id != 7) { // 只等待 piece 消息
        std::ostringstream oss;
        oss << "unexpected message id: " << int(id) << ", length=" << prefix_len;
        cout << oss.str() << endl;
        exit(-1);
    }
    if (prefix_len < 9) {
      cout << "piece message too short" << endl;
      exit(-1);
    }

    memcpy(&piece.piece_index, buf.data() + 5, 4);
    memcpy(&piece.begin_index, buf.data() + 9, 4);
    piece.piece_index = ntohl(piece.piece_index);
    piece.begin_index = ntohl(piece.begin_index);

    size_t block_len = prefix_len - 9;
    cout << "block_len = " << block_len << endl;
    piece.data.assign(reinterpret_cast<const char*>(buf.data()) + 13, block_len);
    cout << "piece data size = " << piece.data.size() << endl;
    return piece;
}


int write_to_file(char *filename, vector<struct Piece> &pieces) {
    ofstream file(filename, ios::binary);
    size_t cnt = 0;
    if (!file) {
      cerr << "Failed to open file\n";
      return 1;
    }
    for(const auto &piece : pieces) {
      file.write(piece.data.c_str(), piece.data.size());
      cnt += piece.data.size();
    }

    cout << "success to write file size = " << cnt << endl;
    return 0;
}