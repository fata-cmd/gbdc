add_library(util OBJECT 
    CNFFormula.h
    ResourceLimits.h
    SolverTypes.h
    Stamp.h
    StreamBuffer.h
    ThreadPool.hh
)

add_library(MallocCount malloc_count.cc)
target_include_directories(MallocCount SYSTEM PUBLIC .)
target_link_libraries(MallocCount PUBLIC dl)