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

struct data_t
{
    size_t time;
    size_t mem;
    size_t jobs;
};

template <typename Extractor>
class ThreadPool
{
private:
    size_t jobs_max;
    std::vector<job_t> jobs;
    std::vector<std::thread> threads;
    std::atomic<std::uint32_t> next_job_idx = 0;
    std::atomic<std::uint32_t> thread_id_counter;
    std::vector<std::atomic<double>> coeffs;
    std::vector<data_t> data;
    std::atomic<bool> can_continue = true;
    std::mutex jobs_m;

    void wait_for_completion()
    {
        size_t begin = std::chrono::steady_clock::now().time_since_epoch().count();
        size_t tp;
        while (threads_working.load(std::memory_order_relaxed))
        {
            tp = std::chrono::steady_clock::now().time_since_epoch().count();
            data.push_back({tp - begin, malloc_count_current(), threads_working.load(std::memory_order_relaxed)});
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (uint i = 0; i < threads.size(); ++i)
            threads[i].join();
        std::cerr << "Peak RSS: " << getPeakRSS() << std::endl;
    }

    void work()
    {
        threads_working.fetch_add(1, std::memory_order_relaxed);
        thread_id = thread_id_counter.fetch_add(1, std::memory_order_relaxed);
        std::uint32_t curr_job_idx;
        while ((curr_job_idx = next_job_idx.fetch_add(1, std::memory_order_relaxed)) < jobs.size())
        {
            update_job_idx(curr_job_idx);
            std::cerr << "Current job index: " << curr_job_idx << "\n";
            job_t &curr_job = jobs[curr_job_idx];
            try
            {
                curr_job.estimate_memory_usage();
                wait_for_starting_permission(curr_job);
                Extractor ex(curr_job.path.c_str());
                ex.extract();
                output_result(curr_job, ex);
                finish_job(curr_job);
            }
            catch (const TerminationRequest &tr)
            {
                std::cerr << tr.what() << "\n";
                can_continue.store(false);
                handle_termination(tr, curr_job);
                requeue_job(curr_job);
            }
        }
        stop_working();
    }

    void stop_working()
    {
        threads_working.fetch_sub(1, std::memory_order_relaxed);
        update_threshold();
    }

    void wait_for_starting_permission(const job_t &job)
    {
        while (!can_start(job) || !can_continue.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    bool can_start(const job_t &job)
    {
        return can_alloc(job.memnbt);
    }

    void update_job_idx(std::uint32_t job_idx)
    {
        thread_data[thread_id].job_idx = job_idx;
        std::uint32_t tmp = last_job_idx.load(std::memory_order_relaxed);
        while (job_idx > tmp && !last_job_idx.compare_exchange_weak(tmp, job_idx, std::memory_order_relaxed, std::memory_order_relaxed))
            ;
    }

    void requeue_job(const job_t &job)
    {
        std::unique_lock<std::mutex> lock(jobs_m);
        jobs.push_back(job);
    }

    void handle_termination(const TerminationRequest &tr, job_t &curr_job)
    {
        curr_job.memnbt = thread_data[thread_id].peak_mem_usage;
        thread_data[thread_id].exception_alloc = false;
        thread_data[thread_id].reset();
        --last_job_idx;
    }

    void finish_job(const job_t &curr_job)
    {
        adjust_coefficients(curr_job);
        unreserve_memory(curr_job.emn);
        can_continue.store(true);
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
        std::cerr << result + "\n Current memory usage: " + std::to_string(thread_data[thread_id].mem_usage) +
                         "\n Estimated memory usage: " + std::to_string(j.emn) +
                         "\n Peak memory usage: " + std::to_string(thread_data[thread_id].peak_mem_usage)
                  << std::endl;
        thread_data[thread_id].reset();
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
            thread_data.emplace_back();
            threads.emplace_back(std::thread(&ThreadPool::work, this));
        }
    }

    void adjust_coefficients(const job_t &j)
    {
    }

public:
    explicit ThreadPool(std::vector<std::string> _hashes, std::uint64_t _mem_max, std::uint32_t _jobs_max)
    {
        // thread_id = thread_id_counter.fetch_and_add(1, std::memory_order_relaxed);
        cancel_me = std::function<bool()>([&]()
                                          { return last_job_idx == thread_data[thread_id].job_idx; });
        mem_max = _mem_max;
        jobs_max = _jobs_max;
        init_jobs(_hashes);
        init_threads(_jobs_max);
        wait_for_completion();
    }
};
