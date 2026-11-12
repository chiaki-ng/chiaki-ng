cmake_minimum_required(VERSION 3.10)

project(rogally)

option(ROGALLY_BUILD_TESTS "Build testing executables for rogally" OFF)

if(WIN32)
    find_library(RUNTIMEOBJECT_LIB runtimeobject REQUIRED)
endif()

add_library(rogally
    include/rogally.h
    src/rogally.cpp
)

target_include_directories(rogally PUBLIC include)

if(WIN32)
    target_link_libraries(rogally PRIVATE ${RUNTIMEOBJECT_LIB})
    target_compile_definitions(rogally PRIVATE ROGALLY_PLATFORM_WINDOWS)
endif()

if(ROGALLY_BUILD_TESTS)
    add_executable(rogally-demo tests/rogally_demo.cpp)
    target_link_libraries(rogally-demo rogally)
endif()
