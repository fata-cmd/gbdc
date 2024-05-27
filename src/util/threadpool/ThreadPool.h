#pragma once

#include <string>
#include <tuple>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>
#include "MPSCQueue.h"
#include "Util.h"
#include <tuple>

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
    using result_t = std::tuple<std::string, std::vector<double>, bool>;
    using extract_t = std::function<std::vector<double, std::allocator<double>>(std::string)>;
    inline constexpr std::uint64_t buffer_per_job = 0; // 1e7;
    inline constexpr std::uint64_t SENTINEL_ID = UINT64_MAX;
    inline constexpr std::memory_order relaxed = std::memory_order_relaxed;

    extern thread_local thread_data_t *tl_data;
    extern thread_local std::uint64_t tl_id;

    extern std::size_t mem_max;
    extern std::atomic<size_t> peak, reserved;
    extern std::atomic<uint16_t> threads_working;
    extern std::mutex termination_ongoing;

    class TerminationRequest : public std::runtime_error
    {
    public:
        explicit TerminationRequest(const std::string &msg) : std::runtime_error(msg) {}
    };

    template <typename Extractor>
    class ThreadPool
    {
    private:
        size_t jobs_max;
        std::vector<job_t> jobs;
        std::vector<std::thread> threads;
        std::vector<thread_data_t> thread_data;
        std::atomic<std::uint32_t> threads_finished = 0;
        std::atomic<std::uint32_t> next_job_idx = 0;
        std::atomic<std::uint32_t> thread_id_counter;
        std::atomic<size_t> termination_counter = 0;
        std::mutex jobs_m;
        MPSCQueue<result_t> results;
        Extractor func;

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
        void output_result(const std::vector<double> result);
        /**
         * @brief Initializes the job queue.
         *
         * @param paths List of paths to instances, for which feature extraction shall be done.
         */
        void init_jobs(const std::vector<std::string> &paths);
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
        explicit ThreadPool(std::uint64_t _mem_max, std::uint32_t _jobs_max, Extractor func, std::vector<std::string> paths);
        
        MPSCQueue<result_t> *get_result_queue();
        /**
         * @brief Called by master thread at the beginning of the program. Frequently samples execution data.
         */
        void start_threadpool();

        bool jobs_completed();
    };

    extern void debug_msg(const std::string &msg);
}
