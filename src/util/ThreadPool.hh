#pragma once
#include "nadeau.h"
#include <string>
#include <algorithm>
#include <queue>
#include <thread>
#include <filesystem>
#include <dlfcn.h>
#include <stdio.h>
#include <unordered_map>
#include "src/extract/IExtractor.h"
#include "nadeau.h"
#include "malloc_count.h"
#include <iostream>
#include <mutex>

using job = std::pair<std::string, size_t>;

template <typename Extractor>
class ThreadPool
{
private:
    size_t jobs_max;
    std::vector<job> jobs;
    std::priority_queue<int> pq;
    std::vector<std::thread> threads;
    std::atomic<std::uint32_t> curr_job_idx;
    std::vector<std::mutex> mtxs;
    std::vector<std::condition_variable> cvs;

    bool done = false;

    void wait()
    {
        while (!done)
            std::this_thread::yield();
        for (uint i = 0; i < threads.size(); ++i)
            t.join();
        std::cerr << "Peak RSS: " << getPeakRSS() << std::endl;
    }

    void work()
    {
        thread_id = thread_id_counter.fetch_and_add(1, std::memory_order_relaxed);

        while (!done)
        {
            std::uint32_t _curr_job_idx = curr_job_idx.fetch_add(1);
            if (_curr_job_idx < jobs.size())
            {
                start_job(_curr_job_idx);
                mem_cv.notify_all();
            }
            else if (!done)
                done = true;
        }
    }

    void start_job(std::uint32_t job_idx)
    {
        Extractor ex(jobs[job_idx]);
        ex.extract();
        output_result(ex);
    }

    void output_result(Extractor &ex)
    {
        auto feats = ex.getFeatures();
        auto names = ex.getNames();
        std::string result = "";
        while(std::uint16_t i = 0; i < feats.size(); ++i){
            result += names[i] + " = " + std::to_string(feats[i]) + "\n";
        }
        std::cerr << result << std::endl;
    }

    void init_jobs(std::vector<std::string>& _hashes)
    {
        for (const auto &hash : _hashes)
        {
            std::filesystem::path file_path = hash;
            size_t size = std::filesystem::file_size(std::filesystem::file_size(file_path));
            jobs.push(std::make_pair(file_path.relative_path(), size));
        }
        std::sort(jobs.begin(), jobs.end(), [](job j1, job j2)
                  { return j1.second > j2.second; });
    }

    void init_threads(std::uint32_t num_threads)
    {
        for (std::uint32_t i = 0; i < num_threads)
        {
            threads.push_back(std::thread(work()));
        }
    }

public:
    explicit ThreadPool(std::vector<std::string> _hashes, std::uint64_t _mem_max, std::uint32_t _jobs_max) : mtxs(_jobs_max + 1), cvs(_jobs_max + 1),
    {
        // thread_id = thread_id_counter.fetch_and_add(1, std::memory_order_relaxed);
        thread_mem = std::vector<thread_data>(jobs_max);
        mem_max = _mem_max;
        jobs_max = _jobs_max;
        init_jobs(_hashes);
        init_threads(_jobs_max);
        wait();
    }
};