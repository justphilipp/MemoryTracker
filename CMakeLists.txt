cmake_minimum_required(VERSION 3.23)
project(Tracker)



# whether to use lock-free tracker, for lock-free data structure memory management purpose
#option(LOCKFREE_TRACKER "Use lock-free tracker." ON)
#set(LOCKFREE_TRACKER 1)

set(CMAKE_CXX_STANDARD 14)



set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wreturn-type -latomic")

add_executable(Tracker main.cpp)

target_link_libraries(Tracker pthread atomic)

