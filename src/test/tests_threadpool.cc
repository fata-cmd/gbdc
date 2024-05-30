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
        threadpool::ThreadPool<threadpool::extract_t> tp(1UL << 25UL, 2U, test_extract, paths);
        auto q = tp.get_result_queue();
        std::thread tp_thread(&threadpool::ThreadPool<threadpool::extract_t>::start_threadpool, &tp);
        auto names = CNF::BaseFeatures("").getNames();
        size_t job_counter = 0;
        while (!tp.jobs_completed() || !q->empty())
        {
            if (!q->empty())
            {
                ++job_counter;
                auto f = q->pop();
                CHECK_EQ(std::get<2>(f),!std::get<1>(f).empty());
                // std::cerr << "Path: " << std::get<0>(f) << "\n";
                // if (std::get<2>(f))
                //     std::cerr << "Features succesfully extracted\n";
                // else
                //     std::cerr << "Features could not be extracted\n";

                // auto features = std::get<1>(f);
                // for (size_t i = 0; i < features.size(); ++i)
                // {
                //     std::cerr << names[i] << "=" << features[i] << "\n";
                // }
            }
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        tp_thread.join();
        CHECK_EQ(paths.size(), job_counter);
    }
}
