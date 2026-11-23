cmake_minimum_required(VERSION 3.16)

# Host tool: prints what a .tracy file contains and checks it (used by
# scripts/run_demo.sh). Built like Tracy's own csvexport, against the
# server library of the Tracy tree given in TRACY_DIR.

set(NO_STATISTICS OFF)
set(TRACY_DIR "${CMAKE_CURRENT_LIST_DIR}/../../tracy" CACHE PATH "Tracy source tree")
include(${TRACY_DIR}/cmake/version.cmake)
set(CMAKE_CXX_STANDARD 20)
project(tracy-trace-info LANGUAGES C CXX VERSION ${TRACY_VERSION_STRING})

include(${TRACY_DIR}/cmake/config.cmake)
include(${TRACY_DIR}/cmake/vendor.cmake)
include(${TRACY_DIR}/cmake/server.cmake)

add_executable(tracy-trace-info trace_info.cpp)
target_link_libraries(tracy-trace-info PRIVATE TracyServer)
