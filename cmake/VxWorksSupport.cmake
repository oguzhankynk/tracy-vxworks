include_guard(GLOBAL)

# VxWorks-specific setup, adapted from vxworks-cpp-template
# (cmake/VxWorksSupport.cmake). Only included when building with
# cmake/toolchains/vxworks.cmake.

if(VX_TARGET_TYPE STREQUAL "DKM")
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
else()
  # Static RTPs run on any kernel with RTP support (no libc.so needed).
  add_link_options(-static)
endif()

# wr-cc adds the sysroot header directories with -I; re-add them with
# -isystem so warnings inside VxWorks headers do not count as ours.
foreach(_dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
  cmake_path(IS_PREFIX CMAKE_SYSROOT "${_dir}" NORMALIZE _in_sysroot)
  if(_in_sysroot)
    add_compile_options("SHELL:-isystem ${_dir}")
  endif()
endforeach()
