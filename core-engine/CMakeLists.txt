cmake_minimum_required(VERSION 3.16)
project(zcmesh_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(zcmesh_core STATIC
    src/arena.cpp
    src/frame.cpp
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
endif()

add_executable(zcmesh_edge src/main.cpp)
target_link_libraries(zcmesh_edge PRIVATE zcmesh_core)

add_executable(zcmesh_bench src/bench.cpp)
target_link_libraries(zcmesh_bench PRIVATE zcmesh_core)

add_executable(zcmesh_hop src/hop.cpp)
target_link_libraries(zcmesh_hop PRIVATE zcmesh_core)

if(MSVC)
    target_compile_options(zcmesh_core PRIVATE /W4)
    target_compile_options(zcmesh_edge PRIVATE /W4)
    target_compile_options(zcmesh_bench PRIVATE /W4)
    target_compile_options(zcmesh_hop PRIVATE /W4)
else()
    target_compile_options(zcmesh_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(zcmesh_edge PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(zcmesh_bench PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(zcmesh_hop PRIVATE -Wall -Wextra -Wpedantic)
endif()
