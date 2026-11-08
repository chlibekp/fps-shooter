# Godot extension (GDExtension) with the TacticalFPS client core.
#   cmake -S Game/native -B build/gdext -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET=template_release
#   cmake --build build/gdext --config Release
# GODOT_CPP_DIR: a godot-cpp checkout (Scripts/godot/build_extension.sh clones it).
cmake_minimum_required(VERSION 3.20)
project(tfps_gdext CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(GODOT_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build/godot-cpp" CACHE PATH "godot-cpp checkout")
add_subdirectory("${GODOT_CPP_DIR}" godot-cpp)

set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../Client/Source/TacticalFPS")
file(GLOB CORE_SOURCES
  "${CORE_DIR}/Private/NetCore/*.cpp"
  "${CORE_DIR}/Private/Audio/*.cpp")

add_library(tfps SHARED
  src/register_types.cpp
  src/tfps_client.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../../Client/NetCoreTests/src/UdpTransport.cpp"
  ${CORE_SOURCES})
target_include_directories(tfps PRIVATE
  "${CORE_DIR}/Public"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../Client/NetCoreTests/src")
target_link_libraries(tfps PRIVATE godot-cpp)
if(WIN32)
  target_link_libraries(tfps PRIVATE ws2_32)
endif()
# The movement code must give bit-identical results to the Rust server.
if(MSVC)
  target_compile_options(tfps PRIVATE /fp:precise)
else()
  target_compile_options(tfps PRIVATE -ffp-contract=off)
endif()

get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set(OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bin")
set_target_properties(tfps PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  PREFIX "lib"
  OUTPUT_NAME "tfps${GODOTCPP_SUFFIX}"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${OUT_DIR}>"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${OUT_DIR}>")
if(APPLE)
  set_target_properties(tfps PROPERTIES SUFFIX ".dylib")
endif()
