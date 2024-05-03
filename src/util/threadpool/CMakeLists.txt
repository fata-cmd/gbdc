# add_library(MallocCount malloc_count.cc)
# target_include_directories(MallocCount SYSTEM PUBLIC .)
# target_link_libraries(MallocCount PUBLIC dl)

message("BUILDING THREADPOOL")
add_library(threadpool SHARED ThreadPool.cc malloc_count.cc)
target_include_directories(threadpool SYSTEM PUBLIC .)