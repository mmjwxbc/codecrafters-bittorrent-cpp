#include "lib/nlohmann/json.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "util.hpp"
using json = nlohmann::json;

json decode_bencoded_value(const std::string &encoded_value, size_t &begin) {
  if (std::isdigit(encoded_value[begin])) {
    // Example: "5:hello" -> "hello"
    size_t colon_index = encoded_value.find(':', begin);
    if (colon_index != std::string::npos) {
      std::string number_string =
          encoded_value.substr(begin, colon_index - begin);
      int64_t number = std::atoll(number_string.c_str());
      std::string str = encoded_value.substr(colon_index + 1, number);
      begin = colon_index + number + 1;
      return json(str);
    } else {
      throw std::runtime_error("Invalid encoded value: " + encoded_value);
    }
  } else if (encoded_value[begin] == 'i') {
    size_t e_index = encoded_value.find('e', begin);
    int64_t number = std::atoll(
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
    throw std::runtime_error("Unhandled encoded value: " + encoded_value);
  }
}

std::vector<uint8_t> extract_info_bytes(const std::string& bencoded_data) {
    size_t pos = 0;

    if (bencoded_data[pos] != 'd') {
        throw std::runtime_error("Invalid torrent file: top-level not a dictionary");
    }
    pos++;

    while (pos < bencoded_data.size()) {
        size_t colon = bencoded_data.find(':', pos);
        if (colon == std::string::npos) throw std::runtime_error("Invalid bencode key");

        std::string key = bencoded_data.substr(pos, colon - pos);
        size_t key_len = std::stoul(key);
        pos = colon + 1 + key_len;

        if (bencoded_data.substr(colon + 1, key_len) == "info") {
            size_t info_start = colon + 1 + key_len;
            int dict_level = 0;
            size_t i = info_start;
            for (; i < bencoded_data.size(); i++) {
                if (bencoded_data[i] == 'd') dict_level++;
                else if (bencoded_data[i] == 'e') {
                    dict_level--;
                    if (dict_level == 0) break;
                }
            }
            if (dict_level != 0) throw std::runtime_error("Unmatched dictionary in info");

            std::vector<uint8_t> info_bytes(bencoded_data.begin() + info_start,
                                            bencoded_data.begin() + i + 1);
            return info_bytes;
        }
    }

    throw std::runtime_error("Info dictionary not found");
}

int main(int argc, char *argv[]) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " decode <encoded_value>" << std::endl;
    return 1;
  }

  std::string command = argv[1];
  size_t begin = 0;
  if (command == "decode") {
    if (argc < 3) {
      std::cerr << "Usage: " << argv[0] << " decode <encoded_value>"
                << std::endl;
      return 1;
    }
    // You can use print statements as follows for debugging, they'll be visible
    // when running tests.
    std::cerr << "Logs from your program will appear here!" << std::endl;

    // Uncomment this block to pass the first stage
    std::string encoded_value = argv[2];
    json array = json::array();
    while (begin < encoded_value.size()) {
      json decoded_value = decode_bencoded_value(encoded_value, begin);
      array.push_back(decoded_value);
    }
    std::cout << array[0].dump() << std::endl;
  } else if (command == "info") {
    std::ifstream file(argv[2], std::ios::binary); // 二进制模式避免换行符转换
    if (!file) {
      std::cerr << "Failed to open file\n";
      return 1;
    }
    std::string encoded_value((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
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
    std::string announce_url = object.at("announce").get<std::string>();
    int64_t length = object.at("info").at("length").get<int64_t>();
    std::vector<uint8_t> info_value = extract_info_bytes(encoded_value);
    std::string info_hash = sha1(info_value);
    std::cout << "Tracker URL: " << announce_url << "\n";
    std::cout << "Length: " << length << "\n";
    std::cout << "Info Hash: " << info_hash << "\n";
  } else {
    std::cerr << "unknown command: " << command << std::endl;
    return 1;
  }

  return 0;
}
