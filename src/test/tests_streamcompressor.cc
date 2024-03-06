#include <stdio.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include "src/util/StreamBuffer.h"
#include "src/util/StreamCompressor.h"

// #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// #include "doctest.h"

// TEST_CASE("StreamCompressor")
// {
//     std::FILE *file;
//     char *name;

//     SUBCASE("Write to archive")
//     {
//         const char *tmp_file = "hello.cnf.xz";
//         auto file = std::filesystem::absolute(tmp_file);
//         StreamCompressor c(file.c_str(), 30, "p cnf 3 4");
//         c.write("\n1 2 0\n1 0\n-2 3 0", 17);
//         c.close();
//         Cl cl1, cl2, cl3;
//         cl1.push_back(Lit(1, false));
//         cl1.push_back(Lit(2, false));
//         cl2.push_back(Lit(1, false));
//         cl3.push_back(Lit(2, true));
//         cl3.push_back(Lit(3, false));
//         std::vector<Cl> clauses{cl1, cl2, cl3};
//         StreamBuffer b(file.c_str());
//         Cl clause;
//         for (int i = 0; b.readClause(clause); ++i)
//         {
//             for (Lit l : clause)
//             {
//                 CHECK(clause == clauses[i]);
//             }
//         }
//         remove("hello.cnf.xz");
//     }

//     SUBCASE("Write from istream to archive")
//     {
//         const char *tmp_file = "src/test/resources/test123.cnf.xz";
//         // const char *cnf_file = "src/test/resources/01bd0865ab694bc71d80b7d285d5777d-shuffling-2-s1480152728-of-bench-sat04-434.used-as.sat04-711.cnf";
//         const char *cnf_file = "src/test/resources/test.cnf";
//         StreamBuffer cnf_buf(cnf_file);

//         StreamCompressor cmpr(tmp_file);
//         std::ifstream in(cnf_file);
//         in >> cmpr;
//         cmpr.close();
//         in.close();
//         StreamBuffer tmp_buf(tmp_file);

//         Cl tmp_cl, cnf_cl;
//         bool eq = true;
//         while (eq)
//         {
//             CHECK((eq = cnf_buf.readClause(cnf_cl) == tmp_buf.readClause(tmp_cl)));
//             CHECK(cnf_cl == tmp_cl);
//         }
//         remove(tmp_file);
//     }
// }
int main()
{
    std::cerr << std::filesystem::current_path() << "\n";
    const char *tmp_arch = "resources/test123.cnf.xz";
    // const char *cnf_file = "src/test/resources/01bd0865ab694bc71d80b7d285d5777d-shuffling-2-s1480152728-of-bench-sat04-434.used-as.sat04-711.cnf";
    const char *cnf_file = "resources/test.cnf.xz";
    StreamBuffer cnf_buf(cnf_file);

    const char *data = "\np cnf 1 2\n1 2 0\n1 0\n-2 3 0";
    size_t data_size = strlen(data);
    StreamCompressor cmpr(tmp_arch, data_size);
    cmpr.write(data, data_size);
    // std::ifstream in(cnf_file);
    // in >> cmpr;
    cmpr.close();
    // in.close();
    StreamBuffer tmp_buf(tmp_arch);

    Cl tmp_cl, cnf_cl;
    while(tmp_buf.readClause(tmp_cl));
    // bool eq = true;
    // while (eq)
    // {
    //     auto a = tmp_buf.skipLine();
    // }
}