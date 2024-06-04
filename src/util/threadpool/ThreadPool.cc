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

    template class ThreadPool<compute_ret_t, std::string,std::string, std::unordered_map<std::string, long>>;
    // template class ThreadPool<std::vector<std::tuple<std::string, std::string, std::variant<double, long, std::string>>>,std::string, std::string, std::unordered_map<std::string, long>>;
    template class ThreadPool<std::string, std::string>;
    template class ThreadPool<std::vector<double>,std::string>;

    thread_local thread_data_t *tl_data;
    thread_local std::uint64_t tl_id = SENTINEL_ID;
    std::mutex termination_ongoing;

    std::atomic<size_t> peak = 0, reserved = 0;
    std::atomic<uint16_t> threads_working = 0;
    size_t job_counter = 0;
    size_t mem_max = 1ULL << 30;

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::start_threadpool()
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

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::work()
    {
        tl_id = thread_id_counter.fetch_add(1, relaxed);
        tl_data = &thread_data[tl_id];
        bool terminated = false;
        while (next_job())
        {
            if (terminated)
            {
                cleanup_termination();
                terminated = false;
            }
            wait_for_starting_permission();
            try
            {
                auto result = std::apply(func, in_process[tl_id].arg);
                output_result(result, true);
                finish_job();
            }
            catch (const TerminationRequest &tr)
            {
                if (strlen(tr.what()) != 0)
                    debug_msg(tr.what());
                requeue_job(tr.memnbt);
                terminated = true;
            }
        }
        if (terminated)
        {
            cleanup_termination();
        }
        finish_work();
    }

    template <typename Ret, typename... Args>
    bool ThreadPool<Ret, Args...>::next_job()
    {
        std::unique_lock<std::mutex> l(jobs_m);
        if (!jobs.empty())
        {
            in_process[tl_id] = jobs.pop();
            return true;
        }
        return false;
    }

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::termination_penalty()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50) * termination_counter.load(relaxed));
    }

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::finish_work()
    {
        ++threads_finished;
    }

    template <typename Ret, typename... Args>
    bool ThreadPool<Ret, Args...>::can_start(size_t size)
    {
        if (can_alloc(size))
        {
            tl_data->set_reserved(size);
            return true;
        }
        return false;
    }

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::wait_for_starting_permission()
    {
        debug_msg("Waiting...\n");
        while (!can_start(in_process[tl_id].memnbt))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ++threads_working;
        debug_msg("Start job with index " + std::to_string(in_process[tl_id].idx) + " ...");
    }

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::requeue_job(size_t memnbt)
    {
        in_process[tl_id].terminate_job(memnbt);
        if (in_process[tl_id].memnbt > mem_max - buffer_per_job)
        {
            debug_msg("Cannot requeue job!\nMemory needed before termination: " + std::to_string(in_process[tl_id].memnbt) + "\nMaximum amount of memory available: " + std::to_string(mem_max - (size_t)1e6));
            output_result({}, false);
            return;
        }
        debug_msg("Requeuing job!\nMemory needed before termination: " + std::to_string(in_process[tl_id].memnbt) + "\nMaximum amount of memory available: " + std::to_string(mem_max - (size_t)1e6));
        std::unique_lock<std::mutex> lock(jobs_m);
        jobs.push(in_process[tl_id]);
    }

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::cleanup_termination()
    {
        finish_job();
        termination_counter.fetch_add(1, relaxed);
        termination_ongoing.unlock();
        termination_penalty();
    }

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::finish_job()
    {
        threads_working.fetch_sub(1, relaxed);
        size_t to_be_unreserved = tl_data->unreserve_memory();
        unreserve_memory(to_be_unreserved);
        tl_data->reset();
    }

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::output_result(const Ret result, const bool success)
    {
        debug_msg("Extraction " + std::string(success ? " " : " not ") + "succesful!\n");
        results.push({result, success});
    }

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::init_jobs(const std::vector<std::tuple<Args...>> &args)
    {
        size_t idx = 0;
        for (const auto &arg : args)
        {
            jobs.push(job_t<Args...>(arg, buffer_per_job, idx++));
        }
    }

    template <typename Ret, typename... Args>
    void ThreadPool<Ret, Args...>::init_threads(std::uint32_t num_threads)
    {
        for (std::uint32_t i = 0; i < num_threads; ++i)
        {
            thread_data.emplace_back();
            threads.emplace_back(std::thread(&ThreadPool::work, this));
        }
    }

    template <typename Ret, typename... Args>
    MPSCQueue<result_t<Ret>> *ThreadPool<Ret, Args...>::get_result_queue()
    {
        return &results;
    }

    template <typename Ret, typename... Args>
    bool ThreadPool<Ret, Args...>::jobs_completed()
    {
        return threads_finished.load(relaxed) == threads.size();
    }

    template <typename Ret, typename... Args>
    ThreadPool<Ret, Args...>::ThreadPool(std::uint64_t _mem_max, std::uint32_t _jobs_max, std::function<Ret(Args...)> _func, std::vector<std::tuple<Args...>> args) : in_process(_jobs_max), jobs_max(_jobs_max), jobs(jobs_max), results(jobs_max), func(_func)
    {
        std::cerr << "Initialising thread pool: \nMemory: " << _mem_max << "\nThreads: " << _jobs_max << std::endl;
        mem_max = _mem_max;
        init_jobs(args);
        init_threads(_jobs_max);
    }

    void debug_msg(const std::string &msg)
    {
#ifndef NDEBUG
        std::cerr << "[Thread " + std::to_string(tl_id) + "] " + msg << std::endl;
#endif
    }
}