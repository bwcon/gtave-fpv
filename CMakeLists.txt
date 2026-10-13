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

add_library(gtaiv_fpv SHARED
  src/main.cpp
  src/autotest.cpp
  src/drone.cpp
  src/recon.cpp
  src/world.cpp
  src/scripthook.cpp
  src/log.cpp
)
set_target_properties(gtaiv_fpv PROPERTIES PREFIX "" OUTPUT_NAME "GTAIV.FPV" SUFFIX ".asi")
target_include_directories(gtaiv_fpv PRIVATE src "${LIBFPV_DIR}/include")
# libfpv.dll is loaded at runtime; the header is only used for declarations.
target_compile_definitions(gtaiv_fpv PRIVATE LIBFPV_STATIC)
target_compile_options(gtaiv_fpv PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_options(gtaiv_fpv PRIVATE -static)
