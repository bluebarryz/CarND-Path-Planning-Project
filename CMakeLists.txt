project(Path_Planning)

cmake_minimum_required (VERSION 3.5)

add_definitions(-std=c++11)

set(CXX_FLAGS "-Wall")
set(CMAKE_CXX_FLAGS, "${CXX_FLAGS}")

set(sources src/main.cpp)


if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin") 

include_directories(/opt/homebrew/include)
include_directories(/opt/homebrew/opt/openssl@3/include)
include_directories(/usr/local/include)
link_directories(/opt/homebrew/lib)
link_directories(/usr/local/lib)
link_directories(/opt/homebrew/opt/openssl@3/lib)
link_directories(/opt/homebrew/Cellar/libuv/1.50.0/lib)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

add_executable(path_planning ${sources})

target_link_libraries(path_planning z ssl uv uWS)
