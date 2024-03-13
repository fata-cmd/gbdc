#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
#include <random>
#include "src/util/SolverTypes.h"

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
            map[key] = stod(value);
        }
    }

    record_file.close();

    return map;
}

char *generate_cnf_file(unsigned num_ccs)
{

    char *cnf_file = strcat(std::tmpnam(nullptr), ".cnf");
    std::ofstream out(cnf_file);
    std::srand(time(nullptr));

    unsigned max_cls_per_cc = 5;
    unsigned max_vars_per_cc = 4;
    unsigned max_lits_per_cl = 10;
    unsigned min_var;
    unsigned cls;
    unsigned lits;
    unsigned rnd;
    bool sign;
    Cl cl;

    out << "p cnf 1 1\n";
    for (unsigned cc_id = 0; cc_id < num_ccs; ++cc_id)
    {
        // determine number of clauses
        cls = (std::rand() % max_cls_per_cc) + 1;
        // determine lower end of range of variables for cc
        min_var = cc_id * max_vars_per_cc + 1;
        for (unsigned cl_id = 0; cl_id < cls; ++cl_id)
        {
            // determine number of lits
            lits = (std::rand() % max_lits_per_cl) + 1;
            for (unsigned i = 0; i < lits; ++i)
            {
                // generate lit
                rnd = (std::rand() % max_vars_per_cc) + min_var;
                sign = std::rand() % 2;
                out << (sign ? "-" : "") << rnd << " ";
            }
            out << "0\n";
        }
    }
    return cnf_file;
}

class XorShift64
{
private:
    std::uint64_t x64;

public:
    explicit XorShift64(const std::uint64_t seed = 88172645463325252ull) : x64(seed) {}

    inline std::uint64_t operator()()
    {
        x64 ^= x64 << 13;
        x64 ^= x64 >> 7;
        x64 ^= x64 << 17;
        return x64;
    }

    inline std::uint64_t operator()(const std::uint64_t range)
    {
        return operator()() % range;
    }
};