cmake_minimum_required(VERSION 3.10)
project(livm VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Your targets here...

add_executable(livm
    src/main.cpp
    src/instructions.cpp
    resources/resources.rc
)

# Add include directory
target_include_directories(livm PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Set C++ standard
set_property(TARGET livm PROPERTY CXX_STANDARD 17)