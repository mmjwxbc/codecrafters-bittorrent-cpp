#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>

#include "lib/nlohmann/json.hpp"

using json = nlohmann::json;

json decode_bencoded_value(const std::string& encoded_value, size_t &begin) {
    if (std::isdigit(encoded_value[begin])) {
        // Example: "5:hello" -> "hello"
        size_t colon_index = encoded_value.find(':', begin);
        if (colon_index != std::string::npos) {
            std::string number_string = encoded_value.substr(begin, colon_index - begin);
            int64_t number = std::atoll(number_string.c_str());
            std::string str = encoded_value.substr(colon_index + 1, number);
            begin = colon_index + number + 1;
            return json(str);
        } else {
            throw std::runtime_error("Invalid encoded value: " + encoded_value);
        }
    } else if(encoded_value[begin] == 'i') {
        size_t e_index = encoded_value.find('e', begin);
        int64_t number = std::atoll(encoded_value.substr(begin + 1, e_index - begin - 1).c_str());
        begin = e_index + 1;
        return json(number);
    } else if(encoded_value[begin] == 'l') {
        json array = json::array();
        begin++;
        while(encoded_value[begin] != 'e') {
            json value = decode_bencoded_value(encoded_value, begin);
            array.push_back(value);
        }
        return array;
    } else {
        throw std::runtime_error("Unhandled encoded value: " + encoded_value);
    }
}

int main(int argc, char* argv[]) {
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
            std::cerr << "Usage: " << argv[0] << " decode <encoded_value>" << std::endl;
            return 1;
        }
        // You can use print statements as follows for debugging, they'll be visible when running tests.
        std::cerr << "Logs from your program will appear here!" << std::endl;

        // Uncomment this block to pass the first stage
        std::string encoded_value = argv[2];
        json decoded_value = decode_bencoded_value(encoded_value, begin);
        std::cout << decoded_value.dump() << std::endl;
    } else {
        std::cerr << "unknown command: " << command << std::endl;
        return 1;
    }

    return 0;
}
