#include <iostream>
#include <array>
#include <cstdio>
#include <unordered_map>
#include <filesystem>
#include <string>

#include "src/extract/CNFBaseFeatures.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

std::unordered_map<std::string, double> expected_records = {
    {"clauses", 1.59684e+06},
    {"variables", 505536},
    {"bytes", 3.29527e+07},
    {"ccs", 1},
    {"cls1", 158},
    {"cls2", 766699},
    {"cls3", 807673},
    {"cls4", 14136},
    {"cls5", 5028},
    {"cls6", 628},
    {"cls7", 1257},
    {"cls8", 0},
    {"cls9", 942},
    {"cls10p", 316},
    {"horn", 984347},
    {"invhorn", 984434},
    {"positive", 293813},
    {"negative", 294057},
    {"hornvars_mean", 4.71768},
    {"hornvars_variance", 59.2435},
    {"hornvars_min", 0},
    {"hornvars_max", 206},
    {"hornvars_entropy", 0.391851},
    {"invhornvars_mean", 4.71762},
    {"invhornvars_variance", 58.8421},
    {"invhornvars_min", 0},
    {"invhornvars_max", 203},
    {"invhornvars_entropy", 0.394555},
    {"balancecls_mean", 0.437081},
    {"balancecls_variance", 0.149243},
    {"balancecls_min", 0},
    {"balancecls_max", 1},
    {"balancecls_entropy", 0.800492},
    {"balancevars_mean", 0.837432},
    {"balancevars_variance", 0.0332151},
    {"balancevars_min", 0.0224719},
    {"balancevars_max", 1},
    {"balancevars_entropy", 0.99904},
    {"vcg_vdegree_mean", 8.08639},
    {"vcg_vdegree_variance", 161.219},
    {"vcg_vdegree_min", 0},
    {"vcg_vdegree_max", 307},
    {"vcg_vdegree_entropy", 0.501417},
    {"vcg_cdegree_mean", 2.56004},
    {"vcg_cdegree_variance", 1.89985},
    {"vcg_cdegree_min", 1},
    {"vcg_cdegree_max", 315},
    {"vcg_cdegree_entropy", 0.335586},
    {"vg_degree_mean", 26.7025},
    {"vg_degree_variance", 1734.65},
    {"vg_degree_min", 0},
    {"vg_degree_max", 785},
    {"vg_degree_entropy", 0.507272},
    {"cg_degree_mean", 71.7411},
    {"cg_degree_variance", 7906.1},
    {"cg_degree_min", 5},
    {"cg_degree_max", 28665},
    {"cg_degree_entropy", 0.782888}};

bool fequal(double a, double b)
{
    double epsilon = fmax(fabs(a), fabs(b)) * 1e-5;
    return fabs(a - b) <= epsilon;
}

TEST_CASE("CNFBaseFeatures")
{
    CNF::BaseFeatures stats("src/test/resources/01bd0865ab694bc71d80b7d285d5777d-shuffling-2-s1480152728-of-bench-sat04-434.used-as.sat04-711.cnf");
    stats.extract();
    std::vector<double> record = stats.getFeatures();
    std::vector<std::string> names = stats.getNames();
    CHECK(record.size() == expected_records.size());
    for (unsigned i = 0; i < record.size(); i++)
    {
        CHECK_MESSAGE(fequal(expected_records[names[i]], record[i]), ("\nUnexpected record for feature '" + names[i] + "'\nExpected: " + std::to_string(expected_records[names[i]]) + "\nActual: " + std::to_string(record[i])));
    }
}
