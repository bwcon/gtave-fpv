cmake_minimum_required(VERSION 3.24)
project(gtaiv_fpv CXX)

# Build with: cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686.cmake
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "GTA IV is 32-bit: configure with cmake/mingw-w64-i686.cmake")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBFPV_DIR "$ENV{HOME}/Projects/libfpv" CACHE PATH "libfpv source checkout")
set(LIBFPV_BUILD "${LIBFPV_DIR}/build/win32" CACHE PATH "libfpv i686 build dir")

# GTAIV.FPV.asi: loader that owns the ScriptHook thread and hot-reloads the core.
add_library(gtaiv_fpv_loader SHARED
  src/loader.cpp
  src/scripthook.cpp
  src/log.cpp
)
set_target_properties(gtaiv_fpv_loader PROPERTIES PREFIX "" OUTPUT_NAME "GTAIV.FPV" SUFFIX ".asi")

# GTAIV.FPV.core.dll: the mod itself.
add_library(gtaiv_fpv_core SHARED
  src/core.cpp
  src/audio.cpp
  src/autotest.cpp
  src/drone.cpp
  src/effects.cpp
  src/fpvapi.cpp
  src/recon.cpp
  src/render.cpp
  src/world.cpp
  src/scripthook.cpp
  src/log.cpp
)
set_target_properties(gtaiv_fpv_core PROPERTIES PREFIX "" OUTPUT_NAME "GTAIV.FPV.core")
target_link_libraries(gtaiv_fpv_core PRIVATE winmm)
# The core appends to the log the loader started.
target_compile_definitions(gtaiv_fpv_core PRIVATE FPVLOG_APPEND)

foreach(target gtaiv_fpv_loader gtaiv_fpv_core)
  target_include_directories(${target} PRIVATE src "${LIBFPV_DIR}/include")
  # libfpv.dll is loaded at runtime; the header is only used for declarations.
  target_compile_definitions(${target} PRIVATE LIBFPV_STATIC)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_options(${target} PRIVATE -static)
endforeach()
