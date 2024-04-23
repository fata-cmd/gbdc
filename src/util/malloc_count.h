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

#ifdef __cplusplus
extern "C"
{ /* for inclusion from C++ */
#endif

    class TerminationRequest : public std::runtime_error
    {
    public:
        TerminationRequest() : std::runtime_error("TerminationRequest") {}
        explicit TerminationRequest(const std::string &msg) : std::runtime_error(msg) {}
    };

    struct job_t
    {
        std::string path;
        size_t file_size;
        size_t emn = 0;    // estimated memory needed
        size_t memnbt = 0; // memory needed before termination
        job_t(std::string _path, size_t _file_size) : path(_path), file_size(_file_size) {}

        void estimate_mem(double coeff)
        {
            if (coeff == 0.0)
                emn = file_size;
            else
                emn = coeff * file_size;
        }
    };

    struct thread_data_t
    {
        thread_data_t() {}
        bool exception_alloc = false; // used to pass allocation checks when calling malloc
        bool waiting = false; // true if waiting for memory to be freed up
        size_t mem_usage = 0; // currently allocated memory
        size_t peak_mem_usage = 0; // peak allocated memory for current job
        size_t num_allocs = 0; // number of calls to malloc for current job
        std::uint32_t job_idx; // index of current job

        void set_job_idx(std::uint32_t _job_idx){
            job_idx = _job_idx;
        }

        void inc_mem(size_t size)
        {
            ++num_allocs;
            mem_usage += size;
            peak_mem_usage = std::max(mem_usage, peak_mem_usage);
        }

        void dec_mem(size_t size)
        {
            mem_usage -= size;
        }

        void reset()
        {
            peak_mem_usage = 0;
            num_allocs = 0;
            exception_alloc = false;
            waiting = false;
        }
    };

    extern thread_local std::uint64_t thread_id;
    extern std::vector<thread_data_t> thread_data;
    extern std::atomic<std::uint32_t> last_job_idx;
    extern std::function<bool()> cancel_me;
    extern long long mem_max;
    extern std::atomic<uint16_t> threads_waiting;
    extern std::atomic<uint16_t> threads_working;

    /* returns the currently allocated amount of memory */
    extern size_t malloc_count_current(void);

    /* returns the current peak memory allocation */
    extern size_t malloc_count_peak(void);

    /* resets the peak memory allocation to current */
    extern void malloc_count_reset_peak(void);

    /* returns the total number of allocations */
    extern size_t malloc_count_num_allocs(void);

    /* returns true if job can fit into memory*/
    extern bool reserve_memory(size_t size);

    /* return reserved memory*/
    extern void unreserve_memory(size_t size);

    /* typedef of callback function */
    typedef void (*malloc_count_callback_type)(void *cookie, size_t current);

    /* supply malloc_count with a callback function that is invoked on each change
     * of the current allocation. The callback function must not use
     * malloc()/realloc()/free() or it will go into an endless recursive loop! */
    extern void malloc_count_set_callback(malloc_count_callback_type cb,
                                          void *cookie);

    /* user function which prints current and peak allocation to stderr */
    extern void malloc_count_print_status(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _MALLOC_COUNT_H_ */

/*****************************************************************************/
;