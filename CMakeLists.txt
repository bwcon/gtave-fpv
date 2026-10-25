cmake_minimum_required(VERSION 3.24)
project(gtave_fpv CXX)

# Build in an MSYS2 MINGW64 shell (x86_64 MinGW-w64):
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build && cmake --build build --target deploy
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "GTA V Enhanced is 64-bit: use a x86_64 MinGW compiler")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBFPV_DIR "${CMAKE_SOURCE_DIR}/../libfpv" CACHE PATH "libfpv source checkout")
set(LIBFPV_DLL "${LIBFPV_DIR}/build/win/libfpv.dll" CACHE FILEPATH "libfpv x86_64 build")
set(GTAVE_DIR "C:/Program Files (x86)/Steam/steamapps/common/Grand Theft Auto V Enhanced" CACHE PATH "GTA V Enhanced game folder")

# GTAVE.FPV.asi: loader that owns the ScriptHookV script and the ReShade
# add-on registration, and hot-reloads the core.
add_library(gtave_fpv_loader SHARED src/loader.cpp src/shv.cpp src/log.cpp)
set_target_properties(gtave_fpv_loader PROPERTIES PREFIX "" OUTPUT_NAME "GTAVE.FPV" SUFFIX ".asi")

# GTAVE.FPV.core.dll: the mod itself.
add_library(gtave_fpv_core SHARED
  src/core.cpp
  src/audio.cpp
  src/drone.cpp
  src/dvr.cpp
  src/fpvapi.cpp
  src/lens.cpp
  src/world.cpp
  src/osd.cpp
  src/radio.cpp
  src/render.cpp
  src/tab.cpp
  src/shv.cpp
  src/log.cpp
)
set_target_properties(gtave_fpv_core PROPERTIES PREFIX "" OUTPUT_NAME "GTAVE.FPV.core")
target_link_libraries(gtave_fpv_core PRIVATE winmm)
# The core appends to the log the loader started.
target_compile_definitions(gtave_fpv_core PRIVATE FPVLOG_APPEND)

foreach(target gtave_fpv_loader gtave_fpv_core)
  target_include_directories(${target} PRIVATE src "${LIBFPV_DIR}/include")
  # Vendored ReShade 6.8.0 + ImGui 1.92.5 headers; SYSTEM hides their MSVC-only warnings.
  target_include_directories(${target} SYSTEM PRIVATE third_party/reshade/include third_party/reshade/imgui)
  # libfpv.dll is loaded at runtime; the header is only used for declarations.
  target_compile_definitions(${target} PRIVATE LIBFPV_STATIC)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_options(${target} PRIVATE -static)
endforeach()

# Copies the mod into the game folder. The running game hot-reloads the core;
# the loader and libfpv.dll are locked while it runs and only change rarely.
add_custom_target(deploy
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:gtave_fpv_core> "${GTAVE_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/shaders/GTAVE_FPV_Lens.fx" "${GTAVE_DIR}/reshade-shaders/Shaders/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/shaders/GTAVE_FPV_Bezel.png" "${GTAVE_DIR}/reshade-shaders/Textures/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:gtave_fpv_loader> "${GTAVE_DIR}/" || ${CMAKE_COMMAND} -E echo "loader in use, kept the running one"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBFPV_DLL}" "${GTAVE_DIR}/" || ${CMAKE_COMMAND} -E echo "libfpv.dll in use, kept the running one"
  DEPENDS gtave_fpv_loader gtave_fpv_core
  VERBATIM)
