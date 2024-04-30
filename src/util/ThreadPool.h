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

void debug_msg(const std::string &msg);

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
    std::atomic<size_t> termination_counter = 0;
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
        thread_id = thread_id_counter.fetch_add(1, std::memory_order_relaxed);
        std::uint32_t curr_job_idx;
        while ((curr_job_idx = next_job_idx.fetch_add(1, std::memory_order_relaxed)) < jobs.size())
        {
            job_t &curr_job = jobs[curr_job_idx];
            try
            {
                wait_for_starting_permission(curr_job);
                update_job_idx(curr_job_idx);
                Extractor ex(curr_job.path.c_str());
                ex.extract();
                output_result(curr_job, ex);
                finish_job(curr_job);
            }
            catch (const TerminationRequest &tr)
            {
                debug_msg(tr.what());
                requeue_job(curr_job);
                cleanup_termination(tr, curr_job);
            }
        }
        stop_working();
    }

    void stop_working()
    {
        ++threads_finished;
    }

    bool can_start(size_t size)
    {
        if (can_alloc(size))
        {
            thread_data[thread_id].inc_reserved(size);
            return true;
        }
        return false;
    }

    void wait_for_starting_permission(const job_t &job)
    {
        while (threads_working.load(std::memory_order_relaxed) != 1 && !can_start(job.memnbt))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ++threads_working;
    }

    void update_job_idx(std::uint32_t job_idx)
    {
        debug_msg("Current job index: " + std::to_string(job_idx) + "\n");
        thread_data[thread_id].job_idx = job_idx;
    }

    void requeue_job(job_t &job)
    {
        if (job.memnbt - mem_max < buffer_per_job)
        {
            debug_msg("Cannot requeue job with path " + job.path + "!\nMemory needed before termination: " + std::to_string(job.memnbt) + "\nMaximum amount of memory available: " + std::to_string(mem_max));
            return;
        }
        debug_msg("Requeuing job with path " + job.path);
        job.memnbt = thread_data[thread_id].peak_mem_allocated;
        ++job.termination_count;
        std::unique_lock<std::mutex> lock(jobs_m);
        jobs.push_back(job);
    }

    void cleanup_termination(const TerminationRequest &tr, job_t &curr_job)
    {
        finish_job(curr_job);
        termination_ongoing.unlock();
        termination_counter.fetch_add(1, std::memory_order_relaxed);
    }

    void finish_job(const job_t &curr_job)
    {
        threads_working.fetch_sub(1, std::memory_order_relaxed);
        unreserve_memory(thread_data[thread_id].mem_reserved);
        thread_data[thread_id].reset();
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
        std::cerr << result + "\n Current memory usage: " + std::to_string(thread_data[thread_id].mem_allocated) +
                         "\n Estimated memory usage: " + std::to_string(j.emn) +
                         "\n Peak memory usage: " + std::to_string(thread_data[thread_id].peak_mem_allocated)
                  << std::endl;
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
        mem_max = _mem_max;
        jobs_max = _jobs_max;
        init_jobs(_hashes);
        init_threads(_jobs_max);
        wait_for_completion();
    }
};

void debug_msg(const std::string &msg)
{
#ifndef NDEBUG
    std::cerr << "[Thread " + std::to_string(thread_id) + "] " + msg << std::endl;
#endif
}