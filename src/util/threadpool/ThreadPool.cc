#include "ThreadPool.h"
#include "Util.h"
#include <string>
#include <algorithm>
#include <thread>
#include <iostream>
#include <mutex>
#include <atomic>
#include "../../extract/CNFBaseFeatures.h"
#include "../../extract/CNFGateFeatures.h"
#include "../../extract/OPBBaseFeatures.h"
#include "../../extract/WCNFBaseFeatures.h"

namespace threadpool
{

    template class ThreadPool<extract_t>;

    thread_local thread_data_t *tl_data;
    thread_local std::uint64_t tl_id = SENTINEL_ID;

    std::atomic<size_t> peak = 0, reserved = 0;
    std::atomic<uint16_t> threads_working = 0;
    std::mutex termination_ongoing;

    size_t mem_max = 1ULL << 30;

    template <typename Extractor>
    void ThreadPool<Extractor>::start_threadpool()
    {
        csv_t csv("data.csv", {"time", "allocated", "reserved", "jobs"});
        size_t tp, total_allocated;
        size_t begin = std::chrono::steady_clock::now().time_since_epoch().count();
        const size_t i = 0;
        while (!jobs_completed())
        {
            total_allocated = 0;
            std::for_each(thread_data.begin(), thread_data.end(), [&](const thread_data_t &td)
                          { total_allocated += td.mem_allocated; });
            tp = std::chrono::steady_clock::now().time_since_epoch().count();
            csv.write_to_file({tp - begin, total_allocated, reserved.load(relaxed), threads_working.load(relaxed)});
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        for (uint i = 0; i < threads.size(); ++i)
            threads[i].join();
        // std::cerr << "Peak RSS: " << getPeakRSS() << std::endl;
    }

    template <typename Extractor>
    void ThreadPool<Extractor>::work()
    {
        tl_id = thread_id_counter.fetch_add(1, relaxed);
        tl_data = &thread_data[tl_id];
        while (next_job())
        {
            try
            {
                wait_for_starting_permission();
                auto result = func(jobs[tl_data->job_idx].path);
                output_result(result);
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
        tl_data->job_idx = next_job_idx.fetch_add(1, relaxed);
        return tl_data->job_idx < jobs.size();
    }

    template <typename Extractor>
    void ThreadPool<Extractor>::termination_penalty()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50) * termination_counter.load(relaxed));
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
            tl_data->set_reserved(size);
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
        debug_msg("Start job " + std::to_string(tl_data->job_idx) + "...\n");
    }

    template <typename Extractor>
    void ThreadPool<Extractor>::requeue_job()
    {
        job_t &job = jobs[tl_data->job_idx];
        job.terminate(std::max(tl_data->peak_mem_allocated, tl_data->mem_reserved));
        if (job.memnbt > mem_max - buffer_per_job)
        {
            debug_msg("Cannot requeue job with path " + job.path + "!\nMemory needed before termination: " + std::to_string(tl_data->peak_mem_allocated) + "\nMaximum amount of memory available: " + std::to_string(mem_max - 1e6));
            return;
        }
        debug_msg("Requeuing job with path " + job.path + "!\nMemory needed before termination: " + std::to_string(tl_data->peak_mem_allocated) + "\nMaximum amount of memory available: " + std::to_string(mem_max - 1e6));
        std::unique_lock<std::mutex> lock(jobs_m);
        jobs.push_back(job);
    }

    template <typename Extractor>
    void ThreadPool<Extractor>::cleanup_termination()
    {
        finish_job();
        termination_counter.fetch_add(1, relaxed);
        termination_ongoing.unlock();
    }

    template <typename Extractor>
    void ThreadPool<Extractor>::finish_job()
    {
        threads_working.fetch_sub(1, relaxed);
        unreserve_memory(tl_data->unreserve_memory());
        tl_data->reset();
    }

    template <typename Extractor>
    void ThreadPool<Extractor>::output_result(const std::vector<double> result)
    {
        const job_t &job = jobs[tl_data->job_idx];
        debug_msg("File " + job.path + " done!\n");
        results.push({job.path, result, true});
        // debug_msg(result + "\nCurrent memory usage: " + std::to_string(tl_data->mem_allocated) +
        //           "\nEstimated memory usage: " + std::to_string(job.emn) +
        //           "\nPeak memory usage: " + std::to_string(tl_data->peak_mem_allocated));
    }

    template <typename Extractor>
    void ThreadPool<Extractor>::init_jobs(const std::vector<std::string> &paths)
    {
        jobs.reserve(paths.size() * 1.5);
        for (const auto &path : paths)
        {
            std::filesystem::path file_path = path;
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
    MPSCQueue<result_t> *ThreadPool<Extractor>::get_result_queue()
    {
        return &results;
    }

    template <typename Extractor>
    bool ThreadPool<Extractor>::jobs_completed()
    {
        return threads_finished.load(relaxed) == threads.size();
    }

    template <typename Extractor>
    ThreadPool<Extractor>::ThreadPool(std::uint64_t _mem_max, std::uint32_t _jobs_max, Extractor _func, std::vector<std::string> paths) : jobs_max(_jobs_max), results(jobs_max), func(_func)
    {
        std::cerr << "Initialising thread pool: \nMemory: " << _mem_max << "\nThreads: " << _jobs_max << std::endl;
        mem_max = _mem_max;
        init_jobs(paths);
        init_threads(_jobs_max);
    }

    void debug_msg(const std::string &msg)
    {
#ifndef NDEBUG
        std::cerr << "[Thread " + std::to_string(tl_id) + "] " + msg << std::endl;
#endif
    }
}