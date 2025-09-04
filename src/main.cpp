#include "lib/nlohmann/json.hpp"
#include <cctype>
#include <cstdint>
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
    handle_peers(torrent, ips, ports);
    for(int i = 0; i < ips.size(); i++) {
      cout << ips[i] << ":" << ports[i] << endl;
    }
    return 0;
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
    // cout << "Tracker URL: " << announce_url << "\n";
    // cout << "Length: " << length << "\n";
    // cout << "Info Hash: " << info_hash << "\n";
    // cout << "Piece Length: " <<  torrent.at("info").at("piece length") << "\n";
    // cout << "Piece Hashes:" << "\n";
    unsigned piece_index = atoi(argv[5]);
    // string info_value = encode_bencode_value(torrent.at("info"));
    vector<string> ips;
    vector<uint16_t> ports;
    handle_peers(torrent, ips, ports);
    int sockfd = handle_handshake(ips[0], ports[0], info_value);
    int64_t piece_length = torrent.at("info").at("piece length").get<int64_t>();
    std::string pieces_str = torrent["info"]["pieces"].get<std::string>();
    int piece_cnt = (length + piece_length) / piece_length;
    int cur_piece_length = (piece_index + 1 == piece_cnt) ? length - (piece_index) * piece_length : piece_length;
    int block_count = (cur_piece_length + 16383) / 16384;
    // cout << "block_count = " << block_count << endl;
    vector<struct Piece> pieces;
    for(int i = 0; i < block_count; i++) {
      int cur_length = (i == block_count - 1) ? cur_piece_length - (i) * 16384 : 16384;
      unsigned begin_index = i * 16384;
      download_block(sockfd, piece_index, begin_index, cur_length);
      struct Piece piece = wait_block(sockfd);
      pieces.emplace_back(piece);
    }
    
    return write_to_file(argv[3], pieces) && handle_wave(sockfd);
  } else if(command == "download") {
    json torrent = decode_torrent_file(argv[4]);
    string announce_url = torrent.at("announce").get<string>();
    int64_t length = torrent.at("info").at("length").get<int64_t>();
    string info_value = encode_bencode_value(torrent.at("info"));
    vector<string> ips;
    vector<uint16_t> ports;
    handle_peers(torrent, ips, ports);
    int sockfd = handle_handshake(ips[0], ports[0], info_value);
    int64_t piece_length = torrent.at("info").at("piece length").get<int64_t>();
    std::string pieces_str = torrent["info"]["pieces"].get<std::string>();
    int piece_cnt = (length + piece_length) / piece_length;
    vector<struct Piece> pieces;
    for(int p = 0; p < piece_cnt; p++) {
      int cur_piece_length = (p + 1 == piece_cnt) ? length - (p) * piece_length : piece_length;
      int block_count = (cur_piece_length + 16383) / 16384;
      // cout << "block_count = " << block_count << endl;
      for(int b = 0; b < block_count; b++) {
        int cur_length = (b == block_count - 1) ? cur_piece_length - (b) * 16384 : 16384;
        unsigned begin_index = b * 16384;
        download_block(sockfd, p, begin_index, cur_length);
        struct Piece piece = wait_block(sockfd);
        pieces.emplace_back(std::move(piece));
      }
    }
    return write_to_file(argv[3], pieces) && handle_wave(sockfd);
  } else if(command == "magnet_parse") {
    string magnet_link = argv[2];
    auto key_val = parse_magnet(magnet_link);
    cout << "Tracker URL: " << key_val["tr"] << endl;
    cout << "Info Hash: " << key_val["xt"] << endl;
  } else if(command == "magnet_handshake") {
    string magnet_link = argv[2];
    auto key_val = parse_magnet(magnet_link);
    vector<string> ips;
    vector<uint16_t> ports;
    handle_magnet_peers(key_val["tr"], key_val["xt"], ips, ports);
    int metadata_id = 0;
    int sockfd = handle_magnet_handshake(ips[0], ports[0], key_val["xt"], metadata_id);
  } else if(command == "magnet_info") {
    string magnet_link = argv[2];
    auto key_val = parse_magnet(magnet_link);
    vector<string> ips;
    vector<uint16_t> ports;
    handle_magnet_peers(key_val["tr"], key_val["xt"], ips, ports);
    int metadata_id = 0;
    int sockfd = handle_magnet_handshake(ips[0], ports[0], key_val["xt"], metadata_id);
    unsigned int piece = 0;
    json metadata = handle_magnet_info(sockfd, metadata_id, piece);
    /*
    Tracker URL: http://bittorrent-test-tracker.codecrafters.io/announce
    Length: 92063
    Info Hash: d69f91e6b2ae4c542468d1073a71d4ea13879a7f
    Piece Length: 32768
    Piece Hashes:
    6e2275e604a0766656736e81ff10b55204ad8d35
    e876f67a2a8886e8f36b136726c30fa29703022d
    f00d937a0213df1982bc8d097227ad9e909acc17
    */
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        std::cout << it.key() << std::endl;
    }
    cout << "Tracker URL: " << key_val["tr"] << "\n";
    cout << "Length: " << metadata.at("length") << "\n";
    cout << "Info Hash: " << key_val["xt"] << "\n";
    cout << "Piece Length: " <<  metadata.at("piece length") << "\n";
    cout << "Piece Hashes:" << "\n";
    string hashes  = metadata.at("pieces").get<string>();
    vector<uint8_t> pieces(hashes.begin(), hashes.end());
    for (size_t i = 0; i < pieces.size(); ++i) {
        if(i % 20 == 0 && i) {
            cout << "\n";
        }
        printf("%02x", pieces[i]);
    }
    cout << "\n";
  } else if(command == "magnet_download_piece") {
    // ./your_program.sh magnet_download_piece -o /tmp/test-piece-0 <magnet-link> 0
    string magnet_link = argv[4];
    auto key_val = parse_magnet(magnet_link);
    vector<string> ips;
    vector<uint16_t> ports;
    handle_magnet_peers(key_val["tr"], key_val["xt"], ips, ports);
    int metadata_id = 0;
    int sockfd = handle_magnet_handshake(ips[0], ports[0], key_val["xt"], metadata_id);
    unsigned piece_index = atoi(argv[5]);
    // cout << "download piece: " << piece_index << "\n";
    json metadata = handle_magnet_info(sockfd, metadata_id, piece_index);
    int64_t piece_length = metadata.at("piece length").get<int64_t>();
    int64_t length = metadata.at("length").get<int64_t>();
    cout << "Tracker URL: " << key_val["tr"] << "\n";
    cout << "Length: " << metadata.at("length") << "\n";
    cout << "Info Hash: " << key_val["xt"] << "\n";
    cout << "Piece Length: " <<  metadata.at("piece length") << "\n";
    cout << "Piece Hashes:" << "\n";
    string hashes  = metadata.at("pieces").get<string>();
    vector<uint8_t> pieces_tmp(hashes.begin(), hashes.end());
    for (size_t i = 0; i < pieces_tmp.size(); ++i) {
        if(i % 20 == 0 && i) {
            cout << "\n";
        }
        printf("%02x", pieces_tmp[i]);
    }
    cout << "\n";
    int piece_cnt = (length + piece_length) / piece_length;
    int cur_piece_length = (piece_index + 1 == piece_cnt) ? length - (piece_index) * piece_length : piece_length;
    int block_count = (cur_piece_length + 16383) / 16384;
    // cout << "block_count = " << block_count << endl;
    vector<struct Piece> pieces;
    for(int i = 0; i < block_count; i++) {
      int cur_length = (i == block_count - 1) ? cur_piece_length - (i) * 16384 : 16384;
      unsigned begin_index = i * 16384;
      download_block(sockfd, piece_index, begin_index, cur_length);
      printf("donwload piece %d\n", i);
      struct Piece piece = wait_block(sockfd);
      pieces.emplace_back(piece);
    }
    
    return write_to_file(argv[3], pieces) && handle_wave(sockfd);
  } else if(command == "magnet_download") {
    string magnet_link = argv[4];
    auto key_val = parse_magnet(magnet_link);
    vector<string> ips;
    vector<uint16_t> ports;
    handle_magnet_peers(key_val["tr"], key_val["xt"], ips, ports);
    int metadata_id = 0;
    int sockfd = handle_magnet_handshake(ips[0], ports[0], key_val["xt"], metadata_id);
    unsigned piece_index = atoi(argv[5]);
    cout << "Fuck" << endl;
    cout << "metadata_id = " << metadata_id << endl;
    json metadata =  handle_magnet_info(sockfd, metadata_id, 0);
    int64_t piece_length = metadata.at("piece length").get<int64_t>();
    int64_t length = metadata.at("length").get<int64_t>();
  } else {
    cerr << "unknown command: " << command << endl;
    return 1;
  }

  return 0;
}
