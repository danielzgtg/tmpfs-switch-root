cmake_minimum_required(VERSION 3.23)
project(tmpfs_switch_root)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)

add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fno-pie
        -fno-stack-protector
        -fcf-protection=none
        -mtune=native
)
add_link_options(-no-pie -Wl,-z,now)

if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
    add_link_options(-s)
endif ()

add_subdirectory(src)
