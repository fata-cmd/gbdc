#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>

std::unordered_map<std::string, double> record_to_map(std::string file_name) {
    std::ifstream file(file_name);
    std::unordered_map<std::string, double> map;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key, value;

        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            map[key] = stod(value);
        }
    }

    file.close();

    return map;
}
