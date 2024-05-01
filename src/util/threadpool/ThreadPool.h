#pragma once

#include <string>
#include <tuple>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>
#include "Util.h"

constexpr std::uint64_t buffer_per_job = 1e7;
constexpr std::uint64_t SENTINEL_ID = UINT64_MAX;

extern thread_local std::uint64_t tl_id;
extern thread_local thread_data_t *tl_data;

extern std::uint64_t mem_max;
extern std::atomic<uint16_t> threads_working;
extern std::mutex termination_ongoing;

/* returns true if memory has been reserved*/
extern bool can_alloc(size_t size);

extern void unreserve_memory(size_t size);

template <typename Extractor>
class ThreadPool
{
public:
    explicit ThreadPool(std::vector<std::string> hashes, std::uint64_t mem_max, std::uint32_t jobs_max);

private:
    void wait_for_completion();
    void work();
    void stop_working();
    bool can_start(size_t size);
    void wait_for_starting_permission(const job_t &job);
    void update_job_idx(std::uint32_t job_idx);
    void requeue_job(job_t &job);
    void cleanup_termination(const TerminationRequest &tr, job_t &curr_job);
    void finish_job(const job_t &curr_job);
    void output_result(const job_t &j, Extractor &ex);
    void init_jobs(const std::vector<std::string> &hashes);
    void init_threads(std::uint32_t num_threads);
};

class TerminationRequest : public std::runtime_error
{
public:
    explicit TerminationRequest(const std::string &msg) : std::runtime_error(msg)
    {
    }
};