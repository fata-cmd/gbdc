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
#include <cassert>
#include <functional>
#include <atomic>
#include <bit>
#include <mutex>
#include "Util.h"
#include "ThreadPool.h"

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
inline constexpr size_t sentinel = 0xDEADC0DE;
inline constexpr size_t TID_MASK = 0xFF00000000000000;
inline constexpr size_t TID_SHIFT = std::__countr_zero(TID_MASK);

/* a simple memory heap for allocations prior to dlsym loading */
#define INIT_HEAP_SIZE 1024 * 1024
static char init_heap[INIT_HEAP_SIZE];
static size_t init_heap_use = 0;

/* output */
#define PPREFIX "malloc_count ### "

using namespace threadpool;

void unreserve_memory(size_t size)
{
    assert((reserved.load(std::memory_order_acquire) >= size));
    assert((reserved.load(std::memory_order_acquire) <= mem_max));
    reserved.fetch_sub(size, std::memory_order_relaxed);
    assert((reserved.load(std::memory_order_acquire) <= mem_max));
}

void inc_allocated(size_t tid, size_t size)
{
    if (tid != tl_id)
    {
        return;
    }
    tl_data->inc_allocated(size);
}

void dec_allocated(size_t tid, size_t size)
{
    if (tid != tl_id)
    {
        return;
    }
    unreserve_memory(tl_data->rmem_not_needed(size));
    tl_data->dec_allocated(size);
}

size_t threshold()
{
    return 0UL;
    //    return buffer_per_job * threads_working.load(relaxed);
}

bool can_alloc(size_t size)
{
    if (tl_data->exception_alloc && size > 10000)
        throw;
    if ((reserved.load(relaxed) > mem_max))
        throw;
    if (size == 0)
        return true;
    bool exchanged = false;
    auto _reserved = reserved.load(relaxed);
    while ((tl_data->exception_alloc || ((_reserved + size + threshold()) <= mem_max)) &&
           !(exchanged = reserved.compare_exchange_weak(_reserved, _reserved + size, relaxed, relaxed)))
        ;
    if ((reserved.load(relaxed) > mem_max))
        throw;

    return exchanged;
}

void terminate(size_t lrq)
{
    // exception allocation needed because of malloc call during libc exception allocation
    tl_data->exception_alloc = true;
    throw TerminationRequest(std::max(tl_data->mem_allocated + lrq, std::max(tl_data->peak_mem_allocated, tl_data->mem_reserved)));
}

/****************************************************/
/* exported symbols that overlay the libc functions */
/****************************************************/

size_t size_and_tid(size_t size)
{
    return size | (tl_id << TID_SHIFT);
}

size_t get_tid(size_t data)
{
    return (data & TID_MASK) >> TID_SHIFT;
}

size_t get_size(size_t data)
{
    return data & ~TID_MASK;
}

size_t get_tid(void *data)
{
    return get_tid(*(size_t *)data);
}

size_t get_size(void *data)
{
    return get_size(*(size_t *)data);
}

/* exported malloc symbol that overrides loading from libc */
extern void *malloc(size_t size)
{
    void *ret;

    if (size == 0)
        return NULL;

    // if (!real_malloc)
        // real_malloc = (malloc_type)dlsym(RTLD_NEXT, "malloc");

    if (real_malloc)
    {
        if (tl_id != UNTRACKED)
        {
            while (!can_alloc(tl_data->rmem_needed(size)))
            {
                if (termination_ongoing.try_lock())
                    terminate(size);
            }
            inc_allocated(tl_id, size);
        }
        /* call read malloc procedure in libc */
        ret = (*real_malloc)(size + alignment);
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
    }
    /* set size to 0 for blocks that have been allocated by untracked threads*/
    *(size_t *)ret = size_and_tid(size);
    *(size_t *)((char *)ret + alignment - sizeof(size_t)) = sentinel;
    return (char *)ret + alignment;
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

    // if (!real_free)
    //     real_free = (free_type)dlsym(RTLD_NEXT, "free");

    if (!real_free)
    {
        fprintf(stderr, PPREFIX "free(%p) outside init heap and without real_free !!!\n", ptr);
        throw;
    }

    ptr = (char *)ptr - alignment;

    if (*(size_t *)((char *)ptr + alignment - sizeof(size_t)) != sentinel)
    {
        std::cerr << "Thread-ID: " << tl_id << "\n";
        fprintf(stderr, PPREFIX "In free(): free(%p) has no sentinel !!! memory corruption?\n", ptr);
        throw;
    }

    if (get_tid(ptr) != UNTRACKED)
    {
        dec_allocated(get_tid(ptr), get_size(ptr));
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
        ptr = (char *)ptr - alignment;

        if (*(size_t *)((char *)ptr + alignment - sizeof(size_t)) != sentinel)
        {
            fprintf(stderr, PPREFIX "realloc(%p) has no sentinel !!! memory corruption?\n",
                    ptr);
            throw;
        }

        oldsize = get_size(ptr);

        if (oldsize >= size)
        {
            /* keep old area, just reduce the size */
            *(size_t *)ptr = size_and_tid(size);
            return (char *)ptr + alignment;
        }
        else
        {
            /* allocate new area and copy data */
            newptr = malloc(size);
            memcpy(newptr, (char *)ptr + alignment, oldsize);
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
        fprintf(stderr, PPREFIX "In realloc(): free(%p) has no sentinel !!! memory corruption?\n", ptr);
        throw;
    }

    oldsize = get_size(ptr);

    if (get_tid(ptr) != UNTRACKED)
    {
        if (oldsize > size)
            dec_allocated(get_tid(ptr), oldsize - size);
        else
            inc_allocated(get_tid(ptr), size - oldsize);
    }

    // if (!real_realloc)
    //     real_realloc = (realloc_type)dlsym(RTLD_NEXT, "realloc");

    newptr = (*real_realloc)(ptr, alignment + size);

    *(size_t *)newptr = size_and_tid(size);

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