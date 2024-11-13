cmake_minimum_required(VERSION 3.12)

project(UDSPSocket LANGUAGES CXX)

add_library(${PROJECT_NAME} STATIC
    "Connection.cpp"
    "Impl.cpp"
    "Impl.hpp"
    "Stream.cpp"
    "StreamTests.cpp"

    "UdpSocket.cpp"
    "UdpSocket.hpp"
    "UdspSocket.hpp"
)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/..")

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Ws2_32
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        pthread
    )
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4 # Warning Level
        /we4456 # Declaration of 'identifier' hides previous local declaration
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-float-conversion
    )
endif()
