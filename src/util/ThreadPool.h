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

struct csv_t
{
    std::ofstream of;
    std::string s = "";
    csv_t(const std::string &filename) : of(filename)
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "Error opening file " << filename << std::endl;
            return;
        }
        file << "Time,Memory,Jobs" << std::endl;
    }

    void write_to_file(std::initializer_list<size_t> data)
    {
        for (const auto &d : data)
        {
            s.append(std::to_string(d) + ",");
        }
        s.pop_back();
        of << s << "\n";
        s.clear();
    }

    void close_file()
    {
        of.close();
    }
};

template <typename Extractor>
class ThreadPool
{
private:
    size_t jobs_max;
    std::vector<job_t> jobs;
    std::vector<std::thread> threads;
    std::atomic<std::uint32_t> threads_finished = 0;
    std::atomic<std::uint32_t> next_job_idx = 0;
    std::atomic<std::uint32_t> thread_id_counter;
    std::vector<std::atomic<double>> coeffs;
    std::atomic<bool> can_continue = true;
    std::mutex jobs_m;

    void wait_for_completion()
    {
        csv_t csv("data.csv");
        size_t begin = std::chrono::steady_clock::now().time_since_epoch().count();
        size_t tp;
        while (threads_finished.load(std::memory_order_relaxed) != threads.size())
        {
            tp = std::chrono::steady_clock::now().time_since_epoch().count();
            // csv.write_to_file({tp - begin, malloc_count_current(), threads_working.load(std::memory_order_relaxed)});
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
            job_t &curr_job = jobs[curr_job_idx];
            try
            {
                curr_job.estimate_memory_usage();
                wait_for_starting_permission(curr_job);
                update_job_idx(curr_job_idx);
                Extractor ex(curr_job.path.c_str());
                ex.extract();
                output_result(curr_job, ex);
                finish_job(curr_job);
            }
            catch (const TerminationRequest &tr)
            {
                std::cerr << tr.what() << "\n";
                can_continue.store(false);
                requeue_job(curr_job);
                cleanup_termination(tr, curr_job);
                // update last_job_index
            }
        }
        stop_working();
    }

    void stop_working()
    {
        ++threads_finished;
        update_threshold();
    }

    void wait_for_starting_permission(const job_t &job)
    {
        while (!can_start(job) || (!can_continue.load(std::memory_order_relaxed) && threads_working.load(std::memory_order_relaxed) != 1))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ++threads_working;
    }

    bool can_start(const job_t &job)
    {
        return can_alloc(job.memnbt);
    }

    void update_job_idx(std::uint32_t job_idx)
    {
        std::cerr << "Current job index: " << job_idx << "\n";
        thread_data[thread_id].job_idx = job_idx;
        std::uint32_t tmp = last_job_idx.load(std::memory_order_relaxed);
        while (job_idx > tmp && !last_job_idx.compare_exchange_weak(tmp, job_idx, std::memory_order_relaxed, std::memory_order_relaxed))
            ;
    }

    void requeue_job(job_t &job)
    {
        if (job.memnbt - mem_max < buffer_per_job)
        {
            std::cerr << "Cannot requeue job with path " << job.path << "!\nMemory needed before termination: " << job.memnbt << "\nMaximum amount of memory available: " << mem_max << std::endl;
            return;
        }
        job.memnbt = thread_data[thread_id].peak_mem_usage;
        std::unique_lock<std::mutex> lock(jobs_m);
        jobs.push_back(job);
    }

    void cleanup_termination(const TerminationRequest &tr, job_t &curr_job)
    {
        thread_data[thread_id].reset();
        thread_data[thread_id].exception_alloc = false;
        --threads_working;
    }

    void finish_job(const job_t &curr_job)
    {
        adjust_coefficients(curr_job);
        unreserve_memory(curr_job.emn);
        can_continue.store(true);
        --threads_working;
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
        std::sort(jobs.begin(), jobs.end(), [](const job_t &j1, const job_t &j2)
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