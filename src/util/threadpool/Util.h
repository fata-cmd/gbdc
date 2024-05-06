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

    template <template <typename> class Container>
    void write_to_file(Container<std::string> data)
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
    void write_to_file(Data data)
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
    job_t(std::string _path, size_t _file_size, size_t buffer_per_job = 0) : path(_path), file_size(_file_size), memnbt(buffer_per_job) {}
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

    size_t mem_needed(size_t size)
    {
        if (mem_reserved < mem_allocated)
        {
            return mem_reserved + size - std::min(mem_reserved + size, mem_allocated);
            // if (mem_reserved + size < mem_allocated)
            // {
            //     return 0UL;
            // }
            // else
            // {
            //     return mem_reserved + size - mem_allocated;
            // }
        }
        return size;
    }
};