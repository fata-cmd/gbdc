#include <string>
#include <algorithm>
#include <queue>
#include <thread>
#include <filesystem>
#include "nadeau.h"
#include <iostream>
#include <mutex>
#include <atomic>
#include <tuple>
#include <condition_variable>
#include "ThreadPool.h"
#include "Util.h"

template <typename Extractor>
void ThreadPool<Extractor>::wait_for_completion()
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

template <typename Extractor>
void ThreadPool<Extractor>::work()
{
    tl_id = thread_id_counter.fetch_add(1, std::memory_order_relaxed);
    tl_data = &thread_data[tl_id];
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

template <typename Extractor>
void ThreadPool<Extractor>::stop_working()
{
    ++threads_finished;
}

template <typename Extractor>
bool ThreadPool<Extractor>::can_start(size_t size)
{
    if (can_alloc(size))
    {
        tl_data->inc_reserved(size);
        return true;
    }
    return false;
}

template <typename Extractor>
void ThreadPool<Extractor>::wait_for_starting_permission(const job_t &job)
{
    while (threads_working.load(std::memory_order_relaxed) != 1 && !can_start(job.memnbt))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ++threads_working;
}

template <typename Extractor>
void ThreadPool<Extractor>::update_job_idx(std::uint32_t job_idx)
{
    debug_msg("Current job index: " + std::to_string(job_idx) + "\n");
    tl_data->job_idx = job_idx;
}

template <typename Extractor>
void ThreadPool<Extractor>::requeue_job(job_t &job)
{
    if (job.memnbt - mem_max < buffer_per_job)
    {
        debug_msg("Cannot requeue job with path " + job.path + "!\nMemory needed before termination: " + std::to_string(job.memnbt) + "\nMaximum amount of memory available: " + std::to_string(mem_max));
        return;
    }
    debug_msg("Requeuing job with path " + job.path);
    job.memnbt = tl_data->peak_mem_allocated;
    ++job.termination_count;
    std::unique_lock<std::mutex> lock(jobs_m);
    jobs.push_back(job);
}

template <typename Extractor>
void ThreadPool<Extractor>::cleanup_termination(const TerminationRequest &tr, job_t &curr_job)
{
    finish_job(curr_job);
    termination_ongoing.unlock();
    termination_counter.fetch_add(1, std::memory_order_relaxed);
}

template <typename Extractor>
void ThreadPool<Extractor>::finish_job(const job_t &curr_job)
{
    threads_working.fetch_sub(1, std::memory_order_relaxed);
    unreserve_memory(tl_data->mem_reserved);
    tl_data->reset();
}

template <typename Extractor>
void ThreadPool<Extractor>::output_result(const job_t &j, Extractor &ex)
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
    std::cerr << result + "\n Current memory usage: " + std::to_string(tl_data->mem_allocated) +
                     "\n Estimated memory usage: " + std::to_string(j.emn) +
                     "\n Peak memory usage: " + std::to_string(tl_data->peak_mem_allocated)
              << std::endl;
}

template <typename Extractor>
void ThreadPool<Extractor>::init_jobs(const std::vector<std::string> &_hashes)
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

template <typename Extractor>
void ThreadPool<Extractor>::init_threads(std::uint32_t num_threads)
{
    for (std::uint32_t i = 0; i < num_threads; ++i)
    {
        thread_data.emplace_back();
        threads.emplace_back(std::thread(&ThreadPool::work, this));
    }
}
template <typename Extractor>
ThreadPool<Extractor>::ThreadPool(std::vector<std::string> _hashes, std::uint64_t _mem_max, std::uint32_t _jobs_max)
{
    mem_max = _mem_max;
    jobs_max = _jobs_max;
    init_jobs(_hashes);
    init_threads(_jobs_max);
    wait_for_completion();
}