#include "lib/nlohmann/json.hpp"
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "util.hpp"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <sys/socket.h>  // socket, connect
#include <unistd.h> 
#include <cmath>
using json = nlohmann::json;
using namespace std;

json decode_torrent_file(char* filename) {
    size_t begin = 0;
    ifstream file(filename, ios::binary);
    if (!file) {
      cerr << filename << " ";
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
    json torrent = decode_torrent_file(argv[2]);
    vector<string> ips;
    vector<uint16_t> ports;
    return handle_peers(torrent, ips, ports);
  } else if(command == "handshake") {
    json torrent = decode_torrent_file(argv[2]);
    string arg(argv[3]);
    size_t pos = arg.find(':');
    string ip = arg.substr(0, pos);
    uint16_t port = atoi(arg.substr(pos + 1).c_str());
    string info_value = encode_bencode_value(torrent.at("info"));
    int sockfd = handle_handshake(ip, port, info_value);
    return handle_wave(sockfd);
  } else if(command == "download_piece") {
    // ./your_program.sh download_piece -o /tmp/test-piece sample.torrent <piece_index>
    json torrent = decode_torrent_file(argv[4]);
    string announce_url = torrent.at("announce").get<string>();
    int64_t length = torrent.at("info").at("length").get<int64_t>();
    string info_value = encode_bencode_value(torrent.at("info"));
    vector<uint8_t> bytes(info_value.begin(), info_value.end());
    string info_hash = sha1(bytes);
    cout << "Tracker URL: " << announce_url << "\n";
    cout << "Length: " << length << "\n";
    cout << "Info Hash: " << info_hash << "\n";
    cout << "Piece Length: " <<  torrent.at("info").at("piece length") << "\n";
    cout << "Piece Hashes:" << "\n";
    unsigned piece_index = atoi(argv[5]);
    // string info_value = encode_bencode_value(torrent.at("info"));
    vector<string> ips;
    vector<uint16_t> ports;
    handle_peers(torrent, ips, ports);
    int sockfd = handle_handshake(ips[0], ports[0], info_value);
    int64_t piece_length = torrent.at("info").at("piece length").get<int64_t>();
    std::string pieces_str = torrent["info"]["pieces"].get<std::string>();
    int block_count = (piece_length + 16383) / 16384;
    cout << "block_count = " << block_count << endl;
    vector<struct Piece> pieces;
    for(int i = 0; i < block_count; i++) {
      int cur_length = (i == block_count - 1) ? piece_length - (i) * 16384 : 16384;
      unsigned begin_index = i * 16384;
      download_block(sockfd, piece_index, begin_index, 16384);
      struct Piece piece = wait_block(sockfd);
      pieces.emplace_back(piece);
    }
    
    return write_to_file(argv[3], pieces) && handle_wave(sockfd);
  } else {
    cerr << "unknown command: " << command << endl;
    return 1;
  }

  return 0;
}
