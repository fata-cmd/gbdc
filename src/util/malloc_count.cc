/******************************************************************************
 * malloc_count.c
 *
 * malloc() allocation counter based on http://ozlabs.org/~jk/code/ and other
 * code preparing LD_PRELOAD shared objects.
 *
 ******************************************************************************
 * Copyright (C) 2013-2014 Timo Bingmann <tb@panthema.net>
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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdint>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>
#include <dlfcn.h>
#include <unordered_map>
#include <thread>
#include "malloc_count.h"
#include <functional>
#include <atomic>
#include <condition_variable>
#include <mutex>

/* user-defined options for output malloc()/free() operations to stderr */

constexpr std::uint64_t SENTINEL_ID = UINT64_MAX;
thread_local std::uint64_t thread_id = SENTINEL_ID;
std::vector<thread_data_t> thread_data;
std::atomic<std::uint32_t> last_job_idx;
std::atomic<uint16_t> threads_working = 0;
std::function<bool()> cancel_me;

std::uint64_t mem_max = 1ULL << 30;
std::uint64_t threshold;

static const int log_operations = 0; /* <-- set this to 1 for log output */
static const size_t log_operations_threshold = 1024 * 1024;

/* option to use gcc's intrinsics to do thread-safe statistics operations */
#define THREAD_SAFE_GCC_INTRINSICS 0

/* to each allocation additional data is added for bookkeeping. due to
 * alignment requirements, we can optionally add more than just one integer. */
static const size_t alignment = 16; /* bytes (>= 2*sizeof(size_t)) */

/* function pointer to the real procedures, loaded using dlsym */
typedef void *(*malloc_type)(size_t);
typedef void (*free_type)(void *);
typedef void *(*realloc_type)(void *, size_t);

static malloc_type real_malloc = NULL;
static free_type real_free = NULL;
static realloc_type real_realloc = NULL;

/* a sentinel value prefixed to each allocation */
static const size_t sentinel = 0xDEADC0DE;

/* a simple memory heap for allocations prior to dlsym loading */
#define INIT_HEAP_SIZE 512 * 512
static char init_heap[INIT_HEAP_SIZE];
static size_t init_heap_use = 0;
static const int log_operations_init_heap = 0;

/* output */
#define PPREFIX "malloc_count ### "

/*****************************************/
/* run-time memory allocation statistics */
/*****************************************/

static std::atomic<size_t> peak = 0, curr = 0, reserved = 0;

static malloc_count_callback_type callback = NULL;
static void *callback_cookie = NULL;
static std::memory_order relaxed = std::memory_order_relaxed;

/* add allocation to statistics */
static void inc_count(size_t inc)
{
    if (thread_id != SENTINEL_ID)
        thread_data[thread_id].inc_mem(inc);
    // if (callback)
    //     callback(callback_cookie, curr);
}

/* decrement allocation to statistics */
static void dec_count(size_t dec)
{
    if (thread_id != SENTINEL_ID)
        thread_data[thread_id].dec_mem(dec);
    // if (callback)
    //     callback(callback_cookie, curr);
}

/* user function to return the currently allocated amount of memory */
extern size_t malloc_count_current(void)
{
    return curr.load(relaxed);
}

/* user function to return the peak allocation */
extern size_t malloc_count_peak(void)
{
    return peak;
}

/* user function to reset the peak allocation to current */
extern void malloc_count_reset_peak(void)
{
    peak.store(curr, relaxed);
}

/* user function which prints current and peak allocation to stderr */
extern void malloc_count_print_status(void)
{
    fprintf(stderr, PPREFIX "current %'lld, peak %'lld\n",
            (long long)curr.load(), (long long)peak.load());
}

/* user function to supply a memory profile callback */
void malloc_count_set_callback(malloc_count_callback_type cb, void *cookie)
{
    callback = cb;
    callback_cookie = cookie;
}

void update_threshold()
{
    threshold = threads_working.load(relaxed) * buffer_per_job; // 10MB per job
}

bool reserve_memory(size_t size)
{
    bool exchanged = false;
    size_t _curr = reserved.load(relaxed);
    while (_curr + size <= mem_max && !(exchanged = reserved.compare_exchange_weak(_curr, _curr + size, relaxed, relaxed)))
        ;
    return exchanged;
}

bool can_alloc(size_t size)
{
    if (mem_max - curr.load(relaxed) < threshold)
        return false;
    bool exchanged = false;
    size_t _curr = curr.load(relaxed);
    while ((thread_data[thread_id].exception_alloc || (_curr + size <= mem_max - threshold)) &&
           !(exchanged = curr.compare_exchange_weak(_curr, _curr + size, relaxed, relaxed)))
        ;
    return exchanged;
}

void terminate(size_t size)
{
    thread_data[thread_id].exception_alloc = true;
    throw TerminationRequest("Thread " + std::to_string(thread_id) + " trying to allocate more memory than currently available! \nCurrent: " +
                             std::to_string(mem_max - std::min(mem_max, curr.load())) + " < Desired: " + std::to_string(size));
}

void unreserve_memory(size_t size)
{
    reserved.fetch_sub(size, relaxed);
}

/****************************************************/
/* exported symbols that overlay the libc functions */
/****************************************************/

/* exported malloc symbol that overrides loading from libc */
extern void *malloc(size_t size)
{
    void *ret;

    if (size == 0)
        return NULL;

    if (real_malloc)
    {
        /* call read malloc procedure in libc */
        ret = (*real_malloc)(alignment + size);

        if (thread_id == SENTINEL_ID)
        {
            inc_count(size);
        }
        else
        {
            while (!can_alloc(size))
            {
                if (!thread_data[thread_id].waiting)
                {
                    threads_working.fetch_sub(1, relaxed);
                    thread_data[thread_id].waiting = true;
                }
                if (threads_waiting.load(relaxed) == threads_working.load(relaxed)) // && cancel_me())
                {
                    terminate(size);
                }
            }
            inc_count(size);
            if (thread_data[thread_id].waiting)
            {
                thread_data[thread_id].waiting = false;
                threads_waiting.fetch_sub(1, relaxed);
            }
        }

        /* prepend allocation size and check sentinel */
        *(size_t *)ret = size;
        *(size_t *)((char *)ret + alignment - sizeof(size_t)) = sentinel;

        return (char *)ret + alignment;
    }
    else
    {
        if (init_heap_use + alignment + size > INIT_HEAP_SIZE)
        {
            fprintf(stderr, PPREFIX "init heap full !!!\n");
            exit(EXIT_FAILURE);
        }

        ret = init_heap + init_heap_use;
        init_heap_use += alignment + size;

        /* prepend allocation size and check sentinel */
        *(size_t *)ret = size;
        *(size_t *)((char *)ret + alignment - sizeof(size_t)) = sentinel;

        return (char *)ret + alignment;
    }
}

/* exported free symbol that overrides loading from libc */
extern void free(void *ptr)
{
    size_t size;

    if (!ptr)
        return; /* free(NULL) is no operation */

    if ((char *)ptr >= init_heap &&
        (char *)ptr <= init_heap + init_heap_use)
    {
        return;
    }

    if (!real_free)
    {
        fprintf(stderr, PPREFIX "free(%p) outside init heap and without real_free !!!\n", ptr);
        return;
    }

    ptr = (char *)ptr - alignment;

    if (*(size_t *)((char *)ptr + alignment - sizeof(size_t)) != sentinel)
    {
        fprintf(stderr, PPREFIX "free(%p) has no sentinel !!! memory corruption?\n", ptr);
    }

    size = *(size_t *)ptr;
    if ((double)size / mem_max > 0.1)
    {
        fprintf(stderr, "Freeing more than 10 percent of mem: %lu\n", size);
    }
    dec_count(size);

    (*real_free)(ptr);
}

/* exported calloc() symbol that overrides loading from libc, implemented using
 * our malloc */
extern void *calloc(size_t nmemb, size_t size)
{
    void *ret;
    size *= nmemb;
    if (!size)
        return NULL;
    ret = malloc(size);
    memset(ret, 0, size);
    return ret;
}

/* exported realloc() symbol that overrides loading from libc */
extern void *realloc(void *ptr, size_t size)
{
    void *newptr;
    size_t oldsize;

    if ((char *)ptr >= (char *)init_heap &&
        (char *)ptr <= (char *)init_heap + init_heap_use)
    {
        ptr = (char *)ptr - alignment;

        if (*(size_t *)((char *)ptr + alignment - sizeof(size_t)) != sentinel)
        {
            fprintf(stderr, PPREFIX "realloc(%p) has no sentinel !!! memory corruption?\n",
                    ptr);
        }

        oldsize = *(size_t *)ptr;

        if (oldsize >= size)
        {
            /* keep old area, just reduce the size */
            *(size_t *)ptr = size;
            return (char *)ptr + alignment;
        }
        else
        {
            /* allocate new area and copy data */
            ptr = (char *)ptr + alignment;
            newptr = malloc(size);
            memcpy(newptr, ptr, oldsize);
            free(ptr);
            return newptr;
        }
    }

    if (size == 0)
    { /* special case size == 0 -> free() */
        free(ptr);
        return NULL;
    }

    if (ptr == NULL)
    { /* special case ptr == 0 -> malloc() */
        return malloc(size);
    }

    ptr = (char *)ptr - alignment;

    if (*(size_t *)((char *)ptr + alignment - sizeof(size_t)) != sentinel)
    {
        fprintf(stderr, PPREFIX "free(%p) has no sentinel !!! memory corruption?\n", ptr);
    }

    oldsize = *(size_t *)ptr;

    if (oldsize > size)
        dec_count(oldsize - size);
    else
        inc_count(size - oldsize);

    newptr = (*real_realloc)(ptr, alignment + size);

    *(size_t *)newptr = size;

    return (char *)newptr + alignment;
}

static __attribute__((constructor)) void init(void)
{
    char *error;

    setlocale(LC_NUMERIC, ""); /* for better readable numbers */

    dlerror();

    real_malloc = (malloc_type)dlsym(RTLD_NEXT, "malloc");
    if ((error = dlerror()) != NULL)
    {
        fprintf(stderr, PPREFIX "error %s\n", error);
        exit(EXIT_FAILURE);
    }

    real_realloc = (realloc_type)dlsym(RTLD_NEXT, "realloc");
    if ((error = dlerror()) != NULL)
    {
        fprintf(stderr, PPREFIX "error %s\n", error);
        exit(EXIT_FAILURE);
    }

    real_free = (free_type)dlsym(RTLD_NEXT, "free");
    if ((error = dlerror()) != NULL)
    {
        fprintf(stderr, PPREFIX "error %s\n", error);
        exit(EXIT_FAILURE);
    }
}

static __attribute__((destructor)) void finish(void)
{
    fprintf(stderr, PPREFIX "exiting, peak: %'lld, current: %'lld\n",
            (long long)peak.load(), (long long)curr.load());
}

/*****************************************************************************/
