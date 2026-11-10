# Godot extension with the TacticalFPS client core. It is written against the
# plain GDExtension C interface (gdapi/gdextension_interface.h, dumped from
# the pinned Godot), so no godot-cpp or code generation is needed.
#   Scripts/godot/build_extension.sh
cmake_minimum_required(VERSION 3.16)
project(tfps_gdext CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(CORE_DIR "${REPO}/Client/Core")
file(GLOB CORE_SOURCES "${CORE_DIR}/src/NetCore/*.cpp" "${CORE_DIR}/src/Audio/*.cpp")

add_library(tfps SHARED
  src/gd_bridge.cpp
  src/tfps_native.cpp
  "${REPO}/Client/NetCoreTests/src/UdpTransport.cpp"
  ${CORE_SOURCES})
target_include_directories(tfps PRIVATE gdapi src "${CORE_DIR}/include" "${REPO}/Client/NetCoreTests/src")
if(WIN32)
  target_link_libraries(tfps PRIVATE ws2_32)
endif()
# Movement must be bit-identical to the Rust server.
if(MSVC)
  target_compile_options(tfps PRIVATE /fp:precise /W3)
else()
  target_compile_options(tfps PRIVATE -ffp-contract=off -Wall -Wextra -Wno-unused-parameter)
  set_target_properties(tfps PROPERTIES CXX_VISIBILITY_PRESET hidden)
endif()
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

# Game/bin/libtfps.<platform>.<arch>.<ext>, as listed in Game/tfps.gdextension.
if(WIN32)
  set(PLATFORM windows)
elseif(APPLE)
  set(PLATFORM macos)
else()
  set(PLATFORM linux)
endif()
set(ARCH x86_64)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  set(ARCH arm64)
endif()
if(APPLE)
  set(ARCH universal)
endif()
set_target_properties(tfps PROPERTIES
  PREFIX "lib"
  OUTPUT_NAME "tfps.${PLATFORM}.${ARCH}"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/../bin>"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/../bin>")
if(APPLE)
  set_target_properties(tfps PROPERTIES SUFFIX ".dylib")
endif()
