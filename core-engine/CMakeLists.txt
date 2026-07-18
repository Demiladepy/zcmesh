cmake_minimum_required(VERSION 3.16)
project(zcmesh_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(zcmesh_core STATIC
    src/arena.cpp
    src/frame.cpp
    src/batch.cpp
    src/net.cpp
    src/router.cpp
)

target_include_directories(zcmesh_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

if(WIN32)
    target_link_libraries(zcmesh_core PUBLIC ws2_32)
    target_compile_definitions(zcmesh_core PUBLIC _WIN32_WINNT=0x0601)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(zcmesh_core PUBLIC Threads::Threads)
endif()

function(zcmesh_bin name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE zcmesh_core)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

zcmesh_bin(zcmesh_edge src/main.cpp)
zcmesh_bin(zcmesh_bench src/bench.cpp)
zcmesh_bin(zcmesh_hop src/hop.cpp)
zcmesh_bin(zcmesh_test_frame src/test_frame.cpp)
zcmesh_bin(zcmesh_capture src/capture.cpp)
zcmesh_bin(zcmesh_replay src/replay.cpp)
zcmesh_bin(zcmesh_inspect src/inspect.cpp)
zcmesh_bin(zcmesh_test_zcm src/test_zcm.cpp)
zcmesh_bin(zcmesh_test_batch src/test_batch.cpp)
zcmesh_bin(zcmesh_test_router src/test_router.cpp)
zcmesh_bin(zcmesh_test_inspect src/test_inspect.cpp)

if(MSVC)
    target_compile_options(zcmesh_core PRIVATE /W4)
else()
    target_compile_options(zcmesh_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME frame_codec COMMAND zcmesh_test_frame)
add_test(NAME zcm_roundtrip COMMAND zcmesh_test_zcm)
add_test(NAME batch_adapt COMMAND zcmesh_test_batch)
add_test(NAME router_state COMMAND zcmesh_test_router)
add_test(NAME inspect_gaps COMMAND zcmesh_test_inspect)
