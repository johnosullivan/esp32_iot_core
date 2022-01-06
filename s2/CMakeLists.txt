# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 14)

include_directories(main/core/include)
include_directories(main/libs/fmt/include)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32mcxx)