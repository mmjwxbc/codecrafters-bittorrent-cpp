#include "lib/nlohmann/json.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "util.hpp"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <sys/socket.h>  // socket, connect
#include <unistd.h> 
using json = nlohmann::json;
using namespace std;
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

json decode_torrent_file(char* filename) {
    size_t begin = 0;
    ifstream file(filename, ios::binary);
    if (!file) {
      cerr << "Failed to open file\n";
      return 1;
    }
    string encoded_value((istreambuf_iterator<char>(file)),
                              istreambuf_iterator<char>());
    json object = json::object();
    if (encoded_value[begin] == 'd') {
      begin++;
      while (encoded_value[begin] != 'e') {
        json key = decode_bencoded_value(encoded_value, begin);
        json value = decode_bencoded_value(encoded_value, begin);
        object[key] = value;
      }
      begin++;
    }
    return object;
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

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    std::string *response = reinterpret_cast<std::string*>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

int main(int argc, char *argv[]) {
  // Flush after every cout / cerr
  cout << unitbuf;
  cerr << unitbuf;

  if (argc < 2) {
    cerr << "Usage: " << argv[0] << " decode <encoded_value>" << endl;
    return 1;
  }

  string command = argv[1];
  size_t begin = 0;
  if (command == "decode") {
    if (argc < 3) {
      cerr << "Usage: " << argv[0] << " decode <encoded_value>"
                << endl;
      return 1;
    }
    // You can use print statements as follows for debugging, they'll be visible
    // when running tests.
    cerr << "Logs from your program will appear here!" << endl;

    // Uncomment this block to pass the first stage
    string encoded_value = argv[2];
    json array = json::array();
    while (begin < encoded_value.size()) {
      json decoded_value = decode_bencoded_value(encoded_value, begin);
      array.push_back(decoded_value);
    }
    cout << array[0].dump() << endl;
  } else if (command == "info") {
    json object = decode_torrent_file(argv[2]);
    string announce_url = object.at("announce").get<string>();
    int64_t length = object.at("info").at("length").get<int64_t>();
    string info_value = encode_bencode_value(object.at("info"));
    vector<uint8_t> bytes(info_value.begin(), info_value.end());
    string info_hash = sha1(bytes);
    cout << "Tracker URL: " << announce_url << "\n";
    cout << "Length: " << length << "\n";
    cout << "Info Hash: " << info_hash << "\n";
    cout << "Piece Length: " <<  object.at("info").at("piece length") << "\n";
    cout << "Piece Hashes:" << "\n";
    string hashes  = object.at("info").at("pieces").get<string>();
    vector<uint8_t> pieces(hashes.begin(), hashes.end());
    for (size_t i = 0; i < pieces.size(); ++i) {
        if(i % 20 == 0 && i) {
            cout << "\n";
        }
        printf("%02x", pieces[i]);
    }
    cout << "\n";
  } else if(command == "peers") {
    json object = decode_torrent_file(argv[2]);
    CURL *curl;
    CURLcode result;
    curl = curl_easy_init();
    if(curl == NULL) {
      cerr << "curl_easy_init() failed" << endl;
      return -1;
    }
    string info_value = encode_bencode_value(object.at("info"));
    vector<uint8_t> bytes(info_value.begin(), info_value.end());
    string announce_url = object.at("announce").get<string>();
    string peer_id = "abcdefghijklmnoptrst";
    int64_t length = object.at("info").at("length").get<int64_t>();
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

      printf("%d.%d.%d.%d:%d\n", ip1, ip2, ip3, ip4, port);
    }
    curl_easy_cleanup(curl);
  } else if(command == "handshake") {
    json torrent = decode_torrent_file(argv[2]);
    string arg(argv[3]);
    size_t pos = arg.find(':');
    string ip = arg.substr(0, pos);
    string port = arg.substr(pos + 1);
    string info_value = encode_bencode_value(torrent.at("info"));
    vector<uint8_t> bytes(info_value.begin(), info_value.end());
    unsigned char hash[SHA_DIGEST_LENGTH]; // 20字节
    SHA1(reinterpret_cast<const unsigned char*>(bytes.data()),
         bytes.size(),
         hash);
    int sockfd = connect(ip, port);
    if(sockfd < 0) return -1;
    char data[68] = {0};
    data[0] = (char)19;
    strncpy(data + 1, "BitTorrent protocol", 19);
    strncpy(data + 28, (char *)hash, SHA_DIGEST_LENGTH);
    strncpy(data + 48, "abcdefghijklmnoptrst", 20);
    send(sockfd, data, 68, 0);
    char buffer[1024] = {0};
    ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    close(sockfd);
    cout << "size n = " << n << endl;
    printf("Peer ID: ");
    for(ssize_t i = 0; i < 20 && i + 48 < n; i++) {
      printf("%02x", buffer[48]);
    }
    printf("\n");
    
  } else {
    cerr << "unknown command: " << command << endl;
    return 1;
  }

  return 0;
}
