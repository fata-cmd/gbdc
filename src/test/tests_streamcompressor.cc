#include <stdio.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include "src/util/StreamBuffer.h"
#include "src/util/StreamCompressor.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

TEST_CASE("StreamCompressor")
{
    SUBCASE("Write to archive")
    {
        std::cerr << std::filesystem::current_path() << "\n";
        const char *tmp_file = strcat(std::tmpnam(nullptr), ".cnf.xz");
        StreamCompressor c(tmp_file, 30);
        const char *data = "p cnf 1 2\n1 2 0\n1 0\n-2 3 0";
        c.write(data, strlen(data));
        c.close();
        Cl cl1, cl2, cl3;
        cl1.push_back(Lit(1, false));
        cl1.push_back(Lit(2, false));
        cl2.push_back(Lit(1, false));
        cl3.push_back(Lit(2, true));
        cl3.push_back(Lit(3, false));
        std::vector<Cl> clauses{cl1, cl2, cl3};
        StreamBuffer b(tmp_file);
        Cl clause;
        for (int i = 0; b.readClause(clause); ++i)
        {
            for (Lit l : clause)
            {
                CHECK(clause == clauses[i]);
            }
        }
        remove(tmp_file);
    }

    SUBCASE("Write from istream to archive")
    {
        const char *tmp_file = strcat(tmpnam(nullptr), ".cnf.xz");
        const char *cnf_file = "src/test/resources/01bd0865ab694bc71d80b7d285d5777d-shuffling-2-s1480152728-of-bench-sat04-434.used-as.sat04-711.cnf";
        // const char *cnf_file = "src/test/resources/test.cnf";
        StreamBuffer cnf_buf(cnf_file);

        StreamCompressor cmpr(tmp_file);
        std::ifstream in(cnf_file);
        in >> cmpr;
        cmpr.close();
        in.close();
        StreamBuffer tmp_buf(tmp_file);

        Cl tmp_cl, cnf_cl;
        bool cnf_cl_read = true, tmp_cl_read = true;
        while (cnf_cl_read && tmp_cl_read)
        {
            cnf_cl_read = cnf_buf.readClause(cnf_cl);
            tmp_cl_read = tmp_buf.readClause(tmp_cl);
            CHECK(cnf_cl == tmp_cl);
        }
        CHECK(cnf_cl_read == tmp_cl_read);
        remove(tmp_file);
    }
}

// int main()
// {
// }