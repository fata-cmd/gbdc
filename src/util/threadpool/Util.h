#pragma once
#include <string>
#include <algorithm>
#include <queue>
#include <thread>
#include <filesystem>
#include <iostream>
#include <fstream>

class csv_t
{
private:
    std::ofstream of;
    std::string s = "";

public:
    csv_t(const std::string &filename, std::initializer_list<std::string> columns) : of(filename)
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "Error opening file " << filename << std::endl;
            return;
        }
        write_to_file(columns);
    }

    void write_to_file(std::initializer_list<std::string> data)
    {
        for (const auto &d : data)
        {
            s.append(d + ",");
        }
        s.pop_back();
        of << s << "\n";
        s.clear();
    }

    template <typename Data>
    void write_to_file(Data &&data)
    {
        for (const auto &d : data)
        {
            s.append(std::to_string(d) + ",");
        }
        s.pop_back();
        of << s << "\n";
        s.clear();
    }

    void write_to_file(std::initializer_list<size_t> data)
    {
        for (const auto &d : data)
        {
            s.append(std::to_string(d) + ",");
        }
        s.pop_back();
        of << s << "\n";
        s.clear();
    }

    void close_file()
    {
        of.close();
    }
};

template <typename... Args>
struct job_t
{
    std::tuple<Args...> args;
    size_t termination_count = 0;
    size_t emn = 0; // estimated memory needed
    size_t memnbt;  // memory needed before termination
    size_t idx;
    job_t() {}
    job_t(std::tuple<Args...> _args, size_t buffer_per_job = 0, size_t _idx = 0) : args(_args), memnbt(buffer_per_job), idx(_idx) {}
    void terminate_job(size_t _memnbt)
    {
        memnbt = _memnbt;
        ++termination_count;
    }
};

struct thread_data_t2
{
    /* needed to pass allocation checks in case of required termination of job */
    bool exception_alloc = false;
    /* currently allocated memory */
    std::atomic<size_t> mem_allocated;
    /* currently reserved memory */
    std::atomic<size_t> mem_reserved;
    /* peak allocated memory for current job */
    size_t peak_mem_allocated;
    /* number of calls to malloc for current job */
    size_t num_allocs;
    thread_data_t2() : mem_allocated(0UL), mem_reserved(0UL), peak_mem_allocated(0UL), num_allocs(0UL) {}
    thread_data_t2(const thread_data_t2 &td)
    {
        exception_alloc = td.exception_alloc;
        mem_allocated.store(td.mem_allocated.load());
        mem_reserved.store(td.mem_reserved.load());
        peak_mem_allocated = td.peak_mem_allocated;
        num_allocs = td.num_allocs;
    }

    void inc_allocated(const size_t size)
    {
        if (size > (1 << 10))
        {
            std::cerr << size << "\n";
        }
        ++num_allocs;
        mem_allocated.fetch_add(size, std::memory_order_acquire);
        peak_mem_allocated = std::max(mem_allocated.load(std::memory_order_release), peak_mem_allocated);
    }

    void dec_allocated(const size_t size)
    {
        mem_allocated.fetch_sub(size, std::memory_order_acquire);
        if (mem_allocated.load(std::memory_order_release) > (1 << 30))
        {
            throw("mem_allocated underflow!\n");
        }
    }

    void set_reserved(const size_t size)
    {
        mem_reserved.store(size, std::memory_order_acq_rel);
    }

    size_t unreserve_memory()
    {
        size_t tmp = mem_reserved.load(std::memory_order_acquire);
        set_reserved(0UL);
        return tmp < mem_allocated.load(std::memory_order_acquire) ? 0UL : tmp - mem_allocated.load(std::memory_order_release);
    }

    void reset()
    {
        exception_alloc = false;
        peak_mem_allocated = 0UL;
        num_allocs = 0UL;
    }

    size_t rmem_needed(const size_t size) const
    {
        // if reserved memory is not sufficient to harbor allocation size,
        // additional reserved memory is needed
        return mem_reserved.load(std::memory_order_acquire) > mem_allocated.load(std::memory_order_acquire) ? std::max(mem_reserved.load(std::memory_order_release), mem_allocated.load(std::memory_order_release) + size) - mem_reserved.load(std::memory_order_release) : size;
    }

    size_t rmem_not_needed(const size_t size) const
    {
        // can return allocated memory down to initially reserved amount
        return mem_allocated.load(std::memory_order_acquire) > mem_reserved.load(std::memory_order_acquire) ? std::min(mem_allocated.load(std::memory_order_release) - mem_reserved.load(std::memory_order_release), size) : 0UL;
    }
};

struct thread_data_t
{
    thread_data_t() {}
    /* needed to pass allocation checks in case of required termination of job */
    bool exception_alloc = false;
    /* currently allocated memory */
    size_t mem_allocated = 0UL;
    /* currently reserved memory */
    size_t mem_reserved = 0UL;
    /* peak allocated memory for current job */
    size_t peak_mem_allocated = 0UL;
    /* number of calls to malloc for current job */
    size_t num_allocs = 0UL;

    void inc_allocated(const size_t size)
    {
        ++num_allocs;
        mem_allocated += size;
        peak_mem_allocated = std::max(mem_allocated, peak_mem_allocated);
    }

    void dec_allocated(const size_t size)
    {
        mem_allocated -= size;
        if (mem_allocated > (1 << 30))
        {
            throw;
        }
    }

    void set_reserved(const size_t size)
    {
        mem_reserved = size;
    }

    size_t unreserve_memory()
    {
        size_t tmp = mem_reserved;
        set_reserved(0UL);
        return tmp < mem_allocated ? 0UL : tmp - mem_allocated;
    }

    void reset()
    {
        exception_alloc = false;
        peak_mem_allocated = 0UL;
        num_allocs = 0UL;
    }

    size_t rmem_needed(const size_t size) const
    {
        // if reserved memory is not sufficient to harbor allocation size,
        // additional reserved memory is needed
        if (mem_reserved > mem_allocated){
            return (mem_reserved - mem_allocated) > size ? 0UL : size - (mem_reserved - mem_allocated);
        }
        return size;
    }

    size_t rmem_not_needed(const size_t size) const
    {
        // can return allocated memory down to initially reserved amount
        return mem_allocated > mem_reserved ? std::min(mem_allocated - mem_reserved, size) : 0UL;
    }
};