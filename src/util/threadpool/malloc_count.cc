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
#include <thread>
#include <functional>
#include <atomic>
#include <mutex>
#include "malloc_count.h"
#include "Util.h"
#include "ThreadPool.h"

/* user-defined options for output malloc()/free() operations to stderr */

constexpr std::uint64_t SENTINEL_ID = UINT64_MAX;
thread_local thread_data_t* tl_data;
thread_local std::uint64_t tl_id = SENTINEL_ID;
std::atomic<std::uint32_t> last_job_idx;
std::atomic<uint16_t> threads_working = 0;
std::mutex termination_ongoing;

std::uint64_t mem_max = 1ULL << 30;

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

/* output */
#define PPREFIX "malloc_count ### "

static std::atomic<size_t> peak = 0, reserved = 0;
TerminationRequest tr("Terminating job..");

static std::memory_order relaxed = std::memory_order_relaxed;

static void inc_allocated(size_t size)
{
    tl_data->inc_allocated(size);
}

static void dec_allocated(size_t size)
{
    tl_data->dec_allocated(size);
    unreserve_memory(std::min(size, tl_data->mem_allocated - tl_data->mem_reserved));
}

size_t threshold()
{
    return buffer_per_job * threads_working.load(relaxed);
}

bool can_alloc(size_t size)
{
    if (size == 0UL)
        return true;
    bool exchanged = false;
    auto _reserved = reserved.load(relaxed);
    while (((_reserved + size + threshold()) <= mem_max) &&
           !(exchanged = reserved.compare_exchange_weak(_reserved, _reserved + size, relaxed, relaxed)))
        ;
    return exchanged;
}

void unreserve_memory(size_t size)
{
    reserved.fetch_sub(size, relaxed);
}

void terminate()
{
    // exception allocation needed because of malloc call during libc exception allocation
    tl_data->exception_alloc = true;
    throw tr;
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
        if (tl_id != SENTINEL_ID)
        {
            // needed memory has not been previously reserved and is not freely allocatable
            if (!tl_data->exception_alloc)
            {
                while (!can_alloc(tl_data->mem_needed(size)))
                {
                    if (termination_ongoing.try_lock())
                        terminate();
                }
            }
            inc_allocated(size);
        }

        /* call read malloc procedure in libc */
        ret = (*real_malloc)(alignment + size);

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
    if (tl_id != SENTINEL_ID)
        dec_allocated(size);

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

    if (tl_id != SENTINEL_ID)
    {
        if (oldsize > size)
            dec_allocated(oldsize - size);
        else
            inc_allocated(size - oldsize);
    }

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
            (long long)peak.load(), (long long)reserved.load());
}

/*****************************************************************************/
