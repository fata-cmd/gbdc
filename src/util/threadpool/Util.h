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
    void write_to_file(Data&& data)
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

struct job_t
{
    std::string path;
    size_t termination_count = 0;
    size_t emn = 0; // estimated memory needed
    size_t file_size;
    size_t memnbt; // memory needed before termination
    size_t idx;
    job_t(){}
    job_t(std::string _path, size_t _file_size, size_t buffer_per_job = 0, size_t _idx = 0) : path(_path), file_size(_file_size), memnbt(buffer_per_job), idx(_idx) {}
    void terminate_job(size_t _memnbt){
        memnbt = _memnbt;
        ++termination_count;
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
    }

    void set_reserved(const size_t size)
    {
        mem_reserved = size;
    }

    size_t unreserve_memory()
    {
        size_t tmp = mem_reserved;
        mem_reserved = 0UL;
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
        return mem_reserved > mem_allocated ? std::max(mem_reserved, mem_allocated + size) - mem_reserved : size;
    }

    size_t rmem_not_needed(const size_t size) const
    {
        // can return allocated memory down to initially reserved amount
        return mem_allocated > mem_reserved ? std::min(mem_allocated - mem_reserved, size) : 0UL;
    }
};