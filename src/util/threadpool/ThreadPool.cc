#include "ThreadPool.h"
#include "Util.h"
#include <string>
#include <algorithm>
#include <thread>
#include "nadeau.h"
#include <iostream>
#include <mutex>
#include <atomic>
#include "../../extract/CNFBaseFeatures.h"
#include "../../extract/CNFGateFeatures.h"
#include "../../extract/OPBBaseFeatures.h"
#include "../../extract/WCNFBaseFeatures.h"

template class ThreadPool<CNF::BaseFeatures>;
template class ThreadPool<WCNF::BaseFeatures>;
template class ThreadPool<OPB::BaseFeatures>;
template class ThreadPool<CNFGateFeatures>;

thread_local thread_data_t *tl_data;
thread_local std::uint64_t tl_id = SENTINEL_ID;

std::atomic<uint16_t> threads_working = 0;
std::mutex termination_ongoing;

std::uint64_t mem_max = 1ULL << 30;

template <typename Extractor>
void ThreadPool<Extractor>::wait_for_completion()
{
    csv_t csv("data.csv", {"time, mem, jobs"});
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
    while (next_job())
    {
        try
        {
            wait_for_starting_permission();
            Extractor ex(jobs[tl_data->job_idx].path.c_str());
            ex.extract();
            output_result(ex);
            finish_job();
        }
        catch (const TerminationRequest &tr)
        {
            debug_msg(tr.what());
            requeue_job();
            cleanup_termination();
            termination_penalty();
        }
    }
    finish_work();
}

template <typename Extractor>
bool ThreadPool<Extractor>::next_job()
{
    do
    {
        tl_data->job_idx = next_job_idx.fetch_add(1, std::memory_order_relaxed);
    } while (jobs[tl_data->job_idx].memnbt >= mem_max - 5e6);
    debug_msg("New job index: " + std::to_string(tl_data->job_idx) + "\n");
    return tl_data->job_idx < jobs.size();
}

template <typename Extractor>
void ThreadPool<Extractor>::termination_penalty()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50) * termination_counter.load(std::memory_order_relaxed));
}

template <typename Extractor>
void ThreadPool<Extractor>::finish_work()
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
void ThreadPool<Extractor>::wait_for_starting_permission()
{
    while (!can_start(jobs[tl_data->job_idx].memnbt))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ++threads_working;
}

template <typename Extractor>
void ThreadPool<Extractor>::requeue_job()
{
    job_t &job = jobs[tl_data->job_idx];
    if (job.memnbt - mem_max < buffer_per_job)
    {
        debug_msg("Cannot requeue job with path " + job.path + "!\nMemory needed before termination: " + std::to_string(job.memnbt) + "\nMaximum amount of memory available: " + std::to_string(mem_max));
        return;
    }
    debug_msg("Requeuing job with path " + job.path);
    job.memnbt = std::max(tl_data->peak_mem_allocated, tl_data->mem_reserved);
    ++job.termination_count;
    std::unique_lock<std::mutex> lock(jobs_m);
    jobs.push_back(job);
}

template <typename Extractor>
void ThreadPool<Extractor>::cleanup_termination()
{
    finish_job();
    termination_ongoing.unlock();
    termination_counter.fetch_add(1, std::memory_order_relaxed);
}

template <typename Extractor>
void ThreadPool<Extractor>::finish_job()
{
    threads_working.fetch_sub(1, std::memory_order_relaxed);
    unreserve_memory(tl_data->unreserve_memory());
    tl_data->reset();
}

template <typename Extractor>
void ThreadPool<Extractor>::output_result(Extractor &ex)
{
    const job_t &job = jobs[tl_data->job_idx];
    auto feats = ex.getFeatures();
    auto names = ex.getNames();
    std::string result = "";
    debug_msg("File: " + job.path + "\nSize: " + std::to_string(job.file_size) + "\n");
    for (std::uint16_t i = 0; i < feats.size(); ++i)
    {
        result += names[i] + " = " + std::to_string(feats[i]) + "\n";
    }
    debug_msg(result + "\nCurrent memory usage: " + std::to_string(tl_data->mem_allocated) +
              "\nEstimated memory usage: " + std::to_string(job.emn) +
              "\nPeak memory usage: " + std::to_string(tl_data->peak_mem_allocated));
}

template <typename Extractor>
void ThreadPool<Extractor>::init_jobs(const std::vector<std::string> &_hashes)
{
    for (const auto &hash : _hashes)
    {
        std::filesystem::path file_path = hash;
        size_t size = std::filesystem::file_size(file_path);
        jobs.push_back(job_t(file_path, size, buffer_per_job));
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

void debug_msg(const std::string &msg)
{
#ifndef NDEBUG
    std::cerr << "[Thread " + std::to_string(tl_id) + "] " + msg << std::endl;
#endif
}