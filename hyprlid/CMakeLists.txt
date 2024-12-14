cmake_minimum_required(VERSION 3.27)

project(hyprlid
    DESCRIPTION "hyprlid plugin for Hyprland"
    VERSION 1.0
)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SRC "*.cpp")

add_library(hyprlid SHARED ${SRC})

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    pixman-1
)
target_link_libraries(hyprlid PRIVATE rt PkgConfig::deps)

install(TARGETS hyprlid)
