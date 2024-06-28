#pragma once

#include <string>
#include <tuple>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <barrier>
#include "MPSCQueue.h"
#include "Util.h"
#include <tuple>
#include <variant>

/**
 * @brief Determines whether the requested amount of memory can be allocated or not,
 * based on current memory consumption.
 *
 * @param size Amount of memory requested
 * @return True if the requested amount of memory has been reserved and can subsequently be allocated.
 * False if the requested amount of memory is too large to fit into the currently available.
 */
extern bool can_alloc(size_t size);
/**
 * @brief Gives back reserved memory.
 *
 * @param size Amount of memory to be given back.
 */
extern void unreserve_memory(size_t size);

namespace threadpool
{
    using compute_ret_t = std::vector<std::tuple<std::string, std::string, std::variant<double, long, std::string>>>;
    using compute_arg_t = std::tuple<std::string, std::string, std::unordered_map<std::string, long>>;
    using compute_t = std::tuple<compute_ret_t, compute_arg_t>;
    using extract_ret_t = std::unordered_map<std::string, std::variant<double, std::string>>;
    ;
    using extract_arg_t = std::string;

    template <typename T>
    // result consists of result of computation, bool indicating whether computation was successful
    // and string to file
    using result_t = std::tuple<T, bool, std::string>;
    inline constexpr std::uint64_t buffer_per_job = 1e7;
    inline constexpr std::uint64_t UNTRACKED = UINT8_MAX;
    inline constexpr std::memory_order relaxed = std::memory_order_relaxed;

    extern thread_local thread_data_t *tl_data;
    extern thread_local std::uint64_t tl_id;

    extern std::size_t mem_max;
    extern std::atomic<size_t> peak, reserved;
    extern std::atomic<uint16_t> threads_working;
    extern std::mutex termination_ongoing;
    inline std::vector<thread_data_t> *tdp;

    class TerminationRequest : public std::runtime_error
    {
    public:
        size_t memnbt;
        explicit TerminationRequest(size_t _memnbt) : memnbt(_memnbt), std::runtime_error("") {}
    };

    template <typename Ret, typename... Args>
    class ThreadPool
    {
    private:
        MPSCQueue<job_t<Args...>> jobs;
        std::vector<std::thread> threads;
        std::vector<thread_data_t> thread_data;
        std::vector<job_t<Args...>> in_process;
        std::atomic<std::uint32_t> thread_id_counter = 0;
        std::atomic<size_t> termination_counter = 0;
        std::mutex jobs_m;
        bool go = false;
        std::shared_ptr<MPSCQueue<result_t<Ret>>> results;
        std::function<Ret(Args...)> func;

        /**
         * @brief Work routine for worker threads.
         */
        void work();
        /**
         * @brief Called after processing all jobs. Makes threads joinable.
         */
        void finish_work();
        /**
         * @brief Subroutine for wait_for_starting_permission()
         *
         * @param size Size of required memory.
         * @return True if memory has been reserved and the job can be started.
         * False if thread has to wait until memory is freed up.
         */
        bool can_start(size_t size);
        /**
         * @brief Determines whether the current job of a thread can be started,
         * based on currently available memory and the (estimated) memory to be required by the job.
         *
         */
        void wait_for_starting_permission();
        /**
         * @brief Called after forceful termination of a job. It stores the peak memory allocated
         * during the execution of the job. This information can later be used to determine
         * more accurately, whether a job can be fit into memory or not. Finally, it locks the job queue
         * and places job at the end of the queue.
         *
         */
        void requeue_job();
        void requeue_job(size_t memnbt);
        /**
         * @brief General cleanup function. Resets the state of the thread such that a new job can be started.
         */
        void cleanup_termination();
        /**
         * @brief Resets the state of the thread such that a new job can be started.
         */
        void termination_penalty();
        bool next_job();
        void finish_job();
        /**
         * @brief Outputs the result of the current job.
         *
         * @param ex Current feature extractor, whose extracted features are to be output.
         */
        void output_result(const Ret result, const bool success);
        /**
         * @brief Initializes the job queue.
         *
         * @param paths List of paths to instances, for which feature extraction shall be done.
         */
        void init_jobs(const std::vector<std::tuple<Args...>> &args);
        /**
         * @brief Initializes worker threads.
         *
         * @param num_threads Number of worker threads
         */
        void init_threads(std::uint32_t num_threads);

    public:
        /**
         * @brief Construct a new ThreadPool object. Initializes the job queue and its worker threads.
         *
         * @param paths List of paths to instances, for which feature extraction shall be done.
         * @param _mem_max Maximum amount of memory available to the ThreadPool.
         * @param _jobs_max Maximum amount of jobs that can be executed in parallel, i.e. number of threads.
         */
        explicit ThreadPool(std::uint64_t _mem_max, std::uint32_t _jobs_max, std::function<Ret(Args...)> func, std::vector<std::tuple<Args...>> args);

        std::shared_ptr<MPSCQueue<result_t<Ret>>> get_result_queue();
        /**
         * @brief Called by master thread at the beginning of the program. Frequently samples execution data.
         */
        void start_threadpool();

        bool jobs_completed();
    };

    extern void debug_msg(const std::string &msg);
}
