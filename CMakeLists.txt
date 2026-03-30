cmake_minimum_required(VERSION 3.16)

project(RKFirmwareStudioCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt5 REQUIRED COMPONENTS Widgets Svg)

add_executable(
  rkstudio_cpp
  cpp/src/paths.cpp
  cpp/src/specs.cpp
  cpp/src/backend.cpp
  cpp/src/qt_window.cpp
  cpp/src/ui_server.cpp
  cpp/src/main.cpp
)

target_include_directories(rkstudio_cpp PRIVATE cpp/include)
target_compile_definitions(rkstudio_cpp PRIVATE RKSTUDIO_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_options(rkstudio_cpp PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rkstudio_cpp PRIVATE Qt5::Widgets Qt5::Svg)
