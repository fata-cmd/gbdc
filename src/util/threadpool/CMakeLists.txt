# add_library(MallocCount malloc_count.cc)
# target_include_directories(MallocCount SYSTEM PUBLIC .)
# target_link_libraries(MallocCount PUBLIC dl)

add_library(threadpool SHARED ThreadPool.cc malloc_count.cc)
target_link_libraries(threadpool PUBLIC ${LIBS} solver)
target_include_directories(threadpool SYSTEM PUBLIC .)
