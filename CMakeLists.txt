cmake_minimum_required(VERSION 3.12)

add_subdirectory("UdspSocket")

if(BUILD_UDSP_SOCKET_TESTS)
    add_subdirectory("tests")
endif()
