#pragma once

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
#include <atomic>
#include <tuple>
#include <condition_variable>


template <typename Extractor>
class ThreadPool
{
private:
    size_t jobs_max;
    std::vector<job_t> jobs;
    std::priority_queue<int> pq;
    std::vector<std::thread> threads;
    std::atomic<std::uint32_t> curr_job_idx;
    std::atomic<std::uint32_t> thread_id_counter;
    std::condition_variable mem_cv;

    Extractor *curr_ex;
    std::atomic<double> coeff_size = 0.0;

    bool done = false;

    void wait()
    {
        while (!done)
            std::this_thread::yield();
        for (uint i = 0; i < threads.size(); ++i)
            threads[i].join();
        std::cerr << "Peak RSS: " << getPeakRSS() << std::endl;
    }

    void work()
    {
        thread_id = thread_id_counter.fetch_add(1, std::memory_order_relaxed);
        job_t *current_job;
        while (!done)
        {
            std::uint32_t _curr_job_idx = curr_job_idx.fetch_add(1, std::memory_order_relaxed);
            if (_curr_job_idx < jobs.size())
            {
                std::cerr << "Current job index: " << curr_job_idx << "\n";
                current_job = &jobs[curr_job_idx];
                current_job->estimate_mem(coeff_size);
                Extractor ex(current_job->path.c_str());
                try
                {
                    ex.extract();
                    output_result(*current_job, ex);
                    finish_job(*current_job);
                }
                catch (const TerminationRequest &tr)
                {
                    std::cerr << tr.what() << "\n";
                    std::cerr << "Destroyed\n";
                    thread_mem[thread_id].exception_alloc = false;
                }
                // start_job(_curr_job_idx);
                // finish_job(*current_job);
            }
            else if (!done)
                done = true;
        }
    }

    // void start_job(std::uint32_t job_idx)
    // {
    //     current_job = &jobs[job_idx];
    //     current_job->estimate_mem(coeff_size);
    //     Extractor ex(jobs[job_idx].path.c_str());
    //     ex.extract();
    //     output_result(jobs[job_idx], ex);
    // }

    void finish_job(const job_t &j)
    {
        adjust_coefficients(j);
        unreserve_memory(j.emn);
        mem_cv.notify_all();
    }

    void output_result(const job_t &j, Extractor &ex)
    {
        auto feats = ex.getFeatures();
        auto names = ex.getNames();
        std::string result = "";
        std::cerr << "File: " << j.path << "\n Size: " << j.file_size << "\n";
        for (std::uint16_t i = 0; i < feats.size(); ++i)
        {
            result += names[i] + " = " + std::to_string(feats[i]) + "\n";
        }
        std::cerr << "FileSize: " << j.file_size << "\n";
        std::cerr << result + "\n Current memory usage: " + std::to_string(thread_mem[thread_id].mem_usage) +
                         "\n Estimated memory usage: " + std::to_string(j.emn) +
                         "\n Peak memory usage: " + std::to_string(thread_mem[thread_id].peak_mem_usage)
                  << std::endl;
        thread_mem[thread_id].reset_peak();
    }

    void init_jobs(const std::vector<std::string> &_hashes)
    {
        for (const auto &hash : _hashes)
        {
            std::filesystem::path file_path = hash;
            size_t size = std::filesystem::file_size(file_path);
            jobs.push_back(job_t(file_path, size));
        }
        std::sort(jobs.begin(), jobs.end(), [](job_t j1, job_t j2)
                  { return j1.file_size < j2.file_size; });
    }

    void init_threads(std::uint32_t num_threads)
    {
        for (std::uint32_t i = 0; i < num_threads; ++i)
        {
            thread_mem.emplace_back();
            threads.emplace_back(std::thread(&ThreadPool::work, this));
        }
    }

    void adjust_coefficients(const job_t &j)
    {
        if (coeff_size.load(std::memory_order_relaxed) == 0.0)
        {
            coeff_size.store((double)thread_mem[thread_id].peak_mem_usage / j.file_size, std::memory_order_relaxed);
        }
        else
        {
            coeff_size.store((double)thread_mem[thread_id].peak_mem_usage / j.file_size + coeff_size.load(std::memory_order_relaxed) / 2.0, std::memory_order_relaxed);
        }
        std::cerr << "Coeff_size = " << coeff_size.load(std::memory_order_relaxed) << "\n";
    }

public:
    explicit ThreadPool(std::vector<std::string> _hashes, std::uint64_t _mem_max, std::uint32_t _jobs_max)
    {
        // thread_id = thread_id_counter.fetch_and_add(1, std::memory_order_relaxed);
        mem_max = _mem_max;
        jobs_max = _jobs_max;
        num_threads = jobs_max;
        init_jobs(_hashes);
        init_threads(_jobs_max);
        wait();
    }
};
