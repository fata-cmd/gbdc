add_library(MallocCount malloc_count.cc)
target_include_directories(MallocCount SYSTEM PUBLIC .)
target_link_libraries(MallocCount PUBLIC dl)

add_library(ThreadPool ThreadPool.cc)
target_include_directories(ThreadPool SYSTEM PUBLIC .)