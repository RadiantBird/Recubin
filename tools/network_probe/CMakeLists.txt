cmake_minimum_required(VERSION 3.26)
project(RecubinNetworkProbe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RECUBIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(ENET_ROOT "${RECUBIN_ROOT}/src/Network/Vendor/enet")

set(ENET_COMMON_SOURCES
    "${ENET_ROOT}/callbacks.c"
    "${ENET_ROOT}/compress.c"
    "${ENET_ROOT}/host.c"
    "${ENET_ROOT}/list.c"
    "${ENET_ROOT}/packet.c"
    "${ENET_ROOT}/peer.c"
    "${ENET_ROOT}/protocol.c"
)

if(WIN32)
    set(ENET_PLATFORM_SOURCE "${ENET_ROOT}/win32.c")
else()
    set(ENET_PLATFORM_SOURCE "${ENET_ROOT}/unix.c")
endif()

add_executable(RecubinNetworkProbe
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${RECUBIN_ROOT}/src/Network/NetworkManager.cpp"
    "${RECUBIN_ROOT}/src/Network/NatProtocol.cpp"
    ${ENET_COMMON_SOURCES}
    ${ENET_PLATFORM_SOURCE}
)

target_include_directories(RecubinNetworkProbe PRIVATE
    "${RECUBIN_ROOT}/include"
)

if(WIN32)
    target_compile_options(RecubinNetworkProbe PRIVATE
        "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
    )
    target_link_libraries(RecubinNetworkProbe PRIVATE ws2_32 winmm)
endif()

if(APPLE)
    target_compile_definitions(RecubinNetworkProbe PRIVATE
        _DARWIN_C_SOURCE
    )
endif()
