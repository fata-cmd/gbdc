#include <iostream>
#include <array>
#include <cstdio>
#include <unordered_map>
#include <filesystem>
#include <string>
#include <thread>

#include "src/test/Util.h"
#include "src/extract/CNFBaseFeatures.h"
#include "src/util/threadpool/ThreadPool.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

namespace tp = threadpool;


static std::vector<double> test_extract(std::string filepath)
{
    CNF::BaseFeatures stats(filepath.c_str());
    stats.extract();
    return stats.getFeatures();
};

static tp::compute_ret_t test_compute(std::string hash, std::string filepath, std::unordered_map<std::string, long> limits)
{
    CNF::BaseFeatures stats(filepath.c_str());
    stats.extract();
    tp::compute_ret_t v;
    for (double f : stats.getFeatures())
        v.emplace_back(hash, filepath, f);
    return v;
};

TEST_CASE("Threadpool_Extract")
{
    SUBCASE("Basefeature extraction")
    {
        namespace fs = std::filesystem;
        const std::string folderPath = "src/test/resources/benchmarks";
        std::vector<std::tuple<std::string>> paths;
        for (const auto &entry : fs::directory_iterator(folderPath))
        {
            paths.push_back(entry.path());
        }
        tp::ThreadPool<std::vector<double>, std::string> tp(1UL << 25UL, 2U, test_extract, paths);
        auto q = tp.get_result_queue();
        std::thread tp_thread(&tp::ThreadPool<std::vector<double>, std::string>::start_threadpool, &tp);
        auto names = CNF::BaseFeatures("").getNames();
        size_t job_counter = 0;
        while (!q->done())
        {
            if (!q->empty())
            {
                ++job_counter;
                auto f = q->pop();
                CHECK_EQ(std::get<1>(f), !std::get<0>(f).empty());
            }
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        tp_thread.join();
        CHECK_EQ(paths.size(), job_counter);
    }
}

