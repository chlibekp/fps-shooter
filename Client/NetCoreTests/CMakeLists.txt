# Standalone build of the engine-independent NetCore used by the Unreal client.
# Lets the C++ netcode and movement be compiled, unit tested, conformance
# tested against the Rust implementation, and run against the real Rust server
# without Unreal Engine.
cmake_minimum_required(VERSION 3.16)
project(TfpsNetCore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Optional: build the whole thing with aggressive float options to prove the
# per-file determinism pragmas work (TFPS_HOSTILE_FLOAT=ON).
option(TFPS_HOSTILE_FLOAT "Compile NetCore with aggressive float options (GCC: -ffp-contract=fast -march=native, MSVC: /fp:fast /arch:AVX2) to stress the determinism pragmas. Note: clang's -ffp-contract=fast ignores pragmas by design, so clang uses -march=native with its default contraction mode." OFF)

set(NETCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Source/TacticalFPS)
file(GLOB NETCORE_SOURCES ${NETCORE_DIR}/Private/NetCore/*.cpp ${NETCORE_DIR}/Private/Ui/*.cpp)

add_library(tfps_netcore STATIC ${NETCORE_SOURCES})
target_include_directories(tfps_netcore PUBLIC ${NETCORE_DIR}/Public)
if(MSVC)
  if(TFPS_HOSTILE_FLOAT)
    # Unreal builds with /fp:fast on MSVC; the NetCore pragmas must still give bit-exact results.
    target_compile_options(tfps_netcore PRIVATE /W4 /WX /fp:fast /arch:AVX2)
  else()
    target_compile_options(tfps_netcore PRIVATE /W4 /WX /fp:precise)
  endif()
else()
  target_compile_options(tfps_netcore PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Werror)
  if(TFPS_HOSTILE_FLOAT AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(tfps_netcore PRIVATE -ffp-contract=fast -march=native)
  elseif(TFPS_HOSTILE_FLOAT)
    target_compile_options(tfps_netcore PRIVATE -march=native)
  else()
    target_compile_options(tfps_netcore PRIVATE -ffp-contract=off)
  endif()
endif()

add_executable(tfps_netcore_tests src/TestMain.cpp src/SimConformanceTest.cpp src/ProtocolTests.cpp src/LiveServerTest.cpp src/MenuTests.cpp)
target_link_libraries(tfps_netcore_tests PRIVATE tfps_netcore)
target_compile_definitions(tfps_netcore_tests PRIVATE TFPS_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(tfps_cli_client src/CliClient.cpp src/UdpTransport.cpp)
target_link_libraries(tfps_cli_client PRIVATE tfps_netcore)
target_compile_definitions(tfps_cli_client PRIVATE TFPS_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_sources(tfps_netcore_tests PRIVATE src/UdpTransport.cpp)
if(WIN32)
  target_link_libraries(tfps_cli_client PRIVATE ws2_32)
  target_link_libraries(tfps_netcore_tests PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME netcore_tests COMMAND tfps_netcore_tests)
