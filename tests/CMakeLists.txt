cmake_minimum_required(VERSION 3.12)

project(UdspTests LANGUAGES CXX)

add_executable(UdspTestClient
    "main_client.cpp"
)
set_property(TARGET UdspTestClient PROPERTY CXX_STANDARD 11)
target_link_libraries(UdspTestClient PRIVATE UDSPSocket)

add_executable(UdspTestServer
    "main_server.cpp"
)
set_property(TARGET UdspTestServer PROPERTY CXX_STANDARD 11)
target_link_libraries(UdspTestServer PRIVATE UDSPSocket)

#if(MSVC)
#    target_compile_options(UdspTestClient PRIVATE
#        /W4 # Warning Level
#    )
#    target_compile_options(UdspTestServer PRIVATE
#        /W4 # Warning Level
#    )
#else()
#    target_compile_options(UdspTestServer PRIVATE
#        -Wall
#        -Wextra
#        -Wpedantic
#        -Wno-float-conversion
#    )
#    target_compile_options(UdspTestServer PRIVATE
#        -Wall
#        -Wextra
#        -Wpedantic
#        -Wno-float-conversion
#    )
#endif()
