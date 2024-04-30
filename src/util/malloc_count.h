/******************************************************************************
 * malloc_count.h
 *
 * Header containing prototypes of user-callable functions to retrieve run-time
 * information about malloc()/free() allocation.
 *
 ******************************************************************************
 * Copyright (C) 2013 Timo Bingmann <tb@panthema.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *****************************************************************************/

#ifndef _MALLOC_COUNT_H_
#define _MALLOC_COUNT_H_

#include <stdlib.h>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <algorithm>

#ifdef __cplusplus
extern "C"
{ /* for inclusion from C++ */
#endif

    class TerminationRequest : public std::runtime_error
    {
    public:
        explicit TerminationRequest(const std::string &msg) : std::runtime_error(msg)
        {
        }
    };

    constexpr std::uint64_t buffer_per_job = 1e6;

    struct job_t
    {
        std::string path;
        size_t termination_count = 0;
        size_t file_size;
        size_t emn = 0;                 // estimated memory needed
        size_t memnbt = buffer_per_job; // memory needed before termination
        job_t(std::string _path, size_t _file_size) : path(_path), file_size(_file_size) {}
    };

    struct thread_data_t
    {
        thread_data_t() {}
        bool exception_alloc = false;
        size_t mem_allocated = 0;      // currently allocated memory
        size_t mem_reserved = 0;       // currently reserved memory
        size_t peak_mem_allocated = 0; // peak reserved memory for current job
        size_t num_allocs = 0;         // number of calls to malloc for current job
        std::uint32_t job_idx = 0;     // index of current job

        void set_job_idx(std::uint32_t _job_idx)
        {
            job_idx = _job_idx;
        }

        void inc_allocated(size_t size)
        {
            ++num_allocs;
            mem_allocated += size;
            peak_mem_allocated = std::max(mem_allocated, peak_mem_allocated);
        }

        void dec_allocated(size_t size)
        {
            mem_allocated -= size;
        }

        void inc_reserved(size_t size)
        {
            mem_reserved += size;
        }

        void dec_reserved(size_t size)
        {
            mem_reserved -= std::min(size, mem_reserved);
        }

        void reset()
        {
            exception_alloc = false;
            mem_reserved = 0;
            peak_mem_allocated = 0;
            num_allocs = 0;
        }
    };

    extern thread_local std::uint64_t thread_id;
    extern std::vector<thread_data_t> thread_data;
    extern std::uint64_t mem_max;
    extern std::atomic<uint16_t> threads_working;
    extern std::mutex termination_ongoing;

    /* returns true if memory has been reserved*/
    extern bool can_alloc(size_t size);

    extern void unreserve_memory(size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _MALLOC_COUNT_H_ */

/*****************************************************************************/
;