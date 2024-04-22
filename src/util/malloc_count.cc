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
std::vector<thread_data> thread_mem;
long long mem_max = 1ULL << 30;
uint16_t num_threads;
std::atomic<uint16_t> threads_waiting = 0;

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

static std::atomic<size_t> peak = 0, curr = 0, total = 0, reserved = 0, num_allocs = 0;

static malloc_count_callback_type callback = NULL;
static void *callback_cookie = NULL;
static std::memory_order mem_order = std::memory_order_seq_cst;

/* add allocation to statistics */
static void inc_count(size_t inc)
{
#if THREAD_SAFE_GCC_INTRINSICS
    long long mycurr = __sync_add_and_fetch(&curr, inc);
    if (mycurr > peak)
        peak = mycurr;
    total += inc;
    if (callback)
        callback(callback_cookie, mycurr);
#else
    if ((curr += inc) > peak)
        peak.store(curr, mem_order);
    total += inc;
    if (thread_id != SENTINEL_ID)
    {
        thread_mem[thread_id].inc_mem(inc);
    }
    if (callback)
        callback(callback_cookie, curr);
#endif
    ++num_allocs;
}

/* decrement allocation to statistics */
static void dec_count(size_t dec)
{
#if THREAD_SAFE_GCC_INTRINSICS
    long long mycurr = __sync_sub_and_fetch(&curr, dec);
    if (callback)
        callback(callback_cookie, mycurr);
#else
    curr -= dec;
    if (thread_id != SENTINEL_ID)
        thread_mem[thread_id].dec_mem(dec);
    if (callback)
        callback(callback_cookie, curr);
#endif
}

/* user function to return the currently allocated amount of memory */
extern size_t malloc_count_current(void)
{
    return curr.load(std::memory_order_relaxed);
}

/* user function to return the peak allocation */
extern size_t malloc_count_peak(void)
{
    return peak;
}

/* user function to reset the peak allocation to current */
extern void malloc_count_reset_peak(void)
{
    peak.store(curr, mem_order);
}

/* user function to return total number of allocations */
extern size_t malloc_count_num_allocs(void)
{
    return num_allocs;
}

/* user function which prints current and peak allocation to stderr */
extern void malloc_count_print_status(void)
{
    fprintf(stderr, PPREFIX "current %'lld, peak %'lld\n",
            (long long)curr.load(), (long long)peak.load());
}

inline void terminate()
{
    throw TerminationRequest("Answering termination request!");
}

/* user function to supply a memory profile callback */
void malloc_count_set_callback(malloc_count_callback_type cb, void *cookie)
{
    callback = cb;
    callback_cookie = cookie;
}

bool reserve_memory(size_t size)
{
    bool exchanged = false;
    size_t _curr = reserved.load(std::memory_order_relaxed);
    while (_curr + size <= mem_max && !(exchanged = reserved.compare_exchange_weak(_curr, _curr + size, std::memory_order_relaxed, std::memory_order_relaxed)))
        ;
    return exchanged;
}

bool can_alloc(size_t size)
{
    bool exchanged = false;
    size_t _curr = curr.load(std::memory_order_relaxed);
    while (_curr + size <= mem_max && !(exchanged = curr.compare_exchange_weak(_curr, _curr + size, std::memory_order_relaxed, std::memory_order_relaxed)))
        ;
    return exchanged;
}

void unreserve_memory(size_t size)
{
    reserved.fetch_sub(size, std::memory_order_relaxed);
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

        while (thread_id != SENTINEL_ID && !thread_mem[thread_id].exception_alloc && !can_alloc(size))
        {
            thread_mem[thread_id].exception_alloc = true;
            throw TerminationRequest("Thread " + std::to_string(thread_id) + " trying to allocate more memory than available! \n" + 
            std::to_string(mem_max) + " < " + std::to_string(curr.load() + size));
            return NULL;
            // uint16_t nr = threads_waiting.fetch_add(1, std::memory_order_relaxed);
            // if (nr == num_threads)
            // {
            //     // abort_youngest_job();
            // }
        }

        inc_count(size);
        if (log_operations && size >= log_operations_threshold)
        {
            fprintf(stderr, PPREFIX "malloc(%'lld) = %p   (current %'lld)\n",
                    (long long)size, (char *)ret + alignment, (long long)curr.load());
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

        if (log_operations_init_heap)
        {
            fprintf(stderr, PPREFIX "malloc(%'lld) = %p   on init heap\n",
                    (long long)size, (char *)ret + alignment);
        }

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
        if (log_operations_init_heap)
        {
            fprintf(stderr, PPREFIX "free(%p)   on init heap\n", ptr);
        }
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
    dec_count(size);

    if (log_operations && size >= log_operations_threshold)
    {
        fprintf(stderr, PPREFIX "free(%p) -> %'lld   (current %'lld)\n",
                ptr, (long long)size, (long long)curr.load());
    }

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
        if (log_operations_init_heap)
        {
            fprintf(stderr, PPREFIX "realloc(%p) = on init heap\n", ptr);
        }

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

    dec_count(oldsize);
    inc_count(size);

    newptr = (*real_realloc)(ptr, alignment + size);

    if (log_operations && size >= log_operations_threshold)
    {
        if (newptr == ptr)
            fprintf(stderr, PPREFIX "realloc(%'lld -> %'lld) = %p   (current %'lld)\n",
                    (long long)oldsize, (long long)size, newptr, (long long)curr.load());
        else
            fprintf(stderr, PPREFIX "realloc(%'lld -> %'lld) = %p -> %p   (current %'lld)\n",
                    (long long)oldsize, (long long)size, ptr, newptr, (long long)curr.load());
    }

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
    fprintf(stderr, PPREFIX "exiting, total: %'lld, peak: %'lld, current: %'lld\n",
            (long long)total.load(), (long long)peak.load(), (long long)curr.load());
}

/*****************************************************************************/
