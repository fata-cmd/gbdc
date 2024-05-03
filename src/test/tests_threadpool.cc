#include <iostream>
#include <array>
#include <cstdio>
#include <unordered_map>
#include <filesystem>
#include <string>

#include "src/test/Util.h"
#include "src/extract/CNFBaseFeatures.h"
#include "src/util/threadpool/ThreadPool.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

TEST_CASE("Threadpool")
{
    SUBCASE("Basefeature extraction")
    {
        namespace fs = std::filesystem;
        const std::string folderPath = "src/test/resources/benchmarks";
        std::vector<std::string> paths;
        for (const auto &entry : fs::directory_iterator(folderPath))
        {
            paths.push_back(entry.path());
        }
        ThreadPool<CNF::BaseFeatures>tp(paths,1UL << 25,1U);
        // ThreadPool<CNF::BaseFeatures>();
        CHECK_EQ(1, 1);
    }
}
