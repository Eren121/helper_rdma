cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(helper_tcp)

add_library(
    helper_rdma
    include/rdma_base.h
    include/rdma_client.h
    include/rdma_server.h
    src/rdma_base.cpp
    src/rdma_client.cpp
    src/rdma_server.cpp)
find_package(Threads REQUIRED)

target_include_directories(helper_rdma PUBLIC include)
target_link_libraries(helper_rdma PUBLIC ibverbs rdmacm Threads::Threads)

add_executable(helper_rdma_test src/main.cpp)
target_link_libraries(helper_rdma_test PUBLIC helper_rdma)