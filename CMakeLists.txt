cmake_minimum_required(VERSION 3.25)

# Tracy client for VxWorks 7 (RTP), plus a small demo program.
#
#   cmake -G Ninja -B build/vx -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/vxworks.cmake
#   cmake --build build/vx
#
# Tracy's sources are expected in ./tracy (scripts/fetch_tracy.sh clones the
# upstream tag and applies patches/tracy-vxworks.patch).

project(tracy_vxworks LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if(VXWORKS)
  include(VxWorksSupport)
endif()

set(TRACY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tracy" CACHE PATH "Patched Tracy source tree")
if(NOT EXISTS "${TRACY_SOURCE_DIR}/public/TracyClient.cpp")
  message(FATAL_ERROR "No Tracy sources in ${TRACY_SOURCE_DIR}; run scripts/fetch_tracy.sh first.")
endif()

# Tracy client options. On VxWorks the patched client needs no TRACY_NO_*
# switches: call stacks, sampling, system tracing, context switches and the
# crash handler are not compiled in for this platform anyway (see README.md).
# Any Tracy option can still be set on the cmake command line.
set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
set(TRACY_STATIC ON CACHE BOOL "" FORCE)
# Keep the process alive after main() until a server has received all data.
set(TRACY_NO_EXIT ON CACHE BOOL "")
if(VX_TARGET_TYPE STREQUAL "DKM")
  # Tracy links Threads::Threads. In the kernel, pthreads are part of the
  # kernel image, but FindThreads (which cannot link a test program for a
  # partially linked module) settles on -lpthreads. Provide an empty target.
  add_library(Threads::Threads INTERFACE IMPORTED)
endif()
add_subdirectory("${TRACY_SOURCE_DIR}" tracy EXCLUDE_FROM_ALL)

add_executable(tracy_demo demo/tracy_demo.cpp)
target_link_libraries(tracy_demo PRIVATE Tracy::TracyClient)

if(VXWORKS AND NOT VX_TARGET_TYPE STREQUAL "DKM")
  add_executable(vxrun_launcher tools/vxrun/launcher.c)
endif()

if(VXWORKS AND NOT VX_TARGET_TYPE STREQUAL "DKM")
  # Prints which OS facilities Tracy relies on are available (porting aid).
  add_executable(vx_probe tools/probe/vx_probe.cpp)
endif()
