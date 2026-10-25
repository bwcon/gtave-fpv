cmake_minimum_required(VERSION 3.24)
project(gtave_fpv_probe CXX)

# Phase 0 spike: ScriptHookV plus the synchronous LOS probe in GTA V Enhanced.
# Build in an MSYS2 MINGW64 shell:
#   cmake -S spikes/probe -B build/probe -G Ninja && cmake --build build/probe
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "GTA V Enhanced is 64-bit: use a x86_64 MinGW compiler")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(GTAVE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../src")

add_library(gtave_fpv_probe SHARED probe.cpp "${GTAVE_SRC}/shv.cpp" "${GTAVE_SRC}/log.cpp")
set_target_properties(gtave_fpv_probe PROPERTIES PREFIX "" OUTPUT_NAME "GTAVE.FPV.probe" SUFFIX ".asi")
target_include_directories(gtave_fpv_probe PRIVATE "${GTAVE_SRC}")
target_compile_options(gtave_fpv_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_options(gtave_fpv_probe PRIVATE -static)
