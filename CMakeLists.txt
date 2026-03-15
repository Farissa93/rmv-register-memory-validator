cmake_minimum_required(VERSION 3.20)
project(RegMemValidator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

include_directories(include)

add_executable(rmv src/main.cpp)

find_package(Threads REQUIRED)
target_link_libraries(rmv PRIVATE Threads::Threads)

install(TARGETS rmv DESTINATION bin)

message(STATUS "RegMemValidator ${PROJECT_VERSION} — C++${CMAKE_CXX_STANDARD}")
