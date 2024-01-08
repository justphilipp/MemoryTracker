cmake_minimum_required(VERSION 3.23)
set(PROJECT_NAME_STR Tracker)
project(${PROJECT_NAME_STR})

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wreturn-type -latomic")

set(EXT_PROJECTS_DIR ${PROJECT_SOURCE_DIR}/ext)

add_subdirectory(${PROJECT_SOURCE_DIR}/test)

#enable_testing()

target_link_libraries(Tracker pthread atomic)

