# simple cmake project to import the library ucx
# and compile a simple program that uses it

cmake_minimum_required(VERSION 3.11)
project(ucx_example)

include(FetchContent)

# find the ucx library
find_package(ucx REQUIRED)

find_package(fmt CONFIG REQUIRED)

# create the executable
add_executable(ucx_server ucx_server.cpp ucx_send_recv.cpp)
# link to ucx
target_link_libraries(ucx_server ucx::ucp ucx::ucs fmt::fmt)

# create the executable
add_executable(ucx_client ucx_client.cpp ucx_send_recv.cpp)
# link to ucx
target_link_libraries(ucx_client ucx::ucp ucx::ucs fmt::fmt)