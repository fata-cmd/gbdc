#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
#include <random>
#include <cstdlib>

double string_to_double(std::string s);

std::unordered_map<std::string, double> record_to_map(std::string record_file_name)
{
    std::ifstream record_file(record_file_name);
    std::unordered_map<std::string, double> map;

    std::string line;
    while (std::getline(record_file, line))
    {
        std::istringstream iss(line);
        std::string key, value;

        if (std::getline(iss, key, '=') && std::getline(iss, value))
        {
            map[key] = string_to_double(value);
        }
    }

    record_file.close();

    return map;
}

double string_to_double(std::string s){
    std::istringstream os(s);
    double d;
    os >> d;
    return d;
}