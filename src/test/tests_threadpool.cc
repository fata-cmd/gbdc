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

static std::vector<double> test_extract(std::string filepath)
{
    CNF::BaseFeatures stats(filepath.c_str());
    stats.extract();
    return stats.getFeatures();
};

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
        threadpool::ThreadPool<threadpool::extract_t> tp(1UL << 25UL, 1U, test_extract, paths);
        tp.start_threadpool();
        // ThreadPool<CNF::BaseFeatures>();
        CHECK_EQ(1, 1);
    }
}
