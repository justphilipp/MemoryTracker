cmake_minimum_required(VERSION 3.23)
set(PROJECT_NAME_STR Tracker)
project(${PROJECT_NAME_STR})

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wreturn-type -latomic")

set(EXT_PROJECTS_DIR ${PROJECT_SOURCE_DIR}/ext)

add_subdirectory(${PROJECT_SOURCE_DIR}/test)

add_subdirectory(${EXT_PROJECTS_DIR}/ARIMA)

#target_link_directories(Tracker ${EXT_PROJECTS_DIR}/ARIMA)


target_link_libraries(Tracker pthread atomic)

add_executable(main main.cpp ${EXT_PROJECTS_DIR}/ARIMA/ARMAMath.cpp ${EXT_PROJECTS_DIR}/ARIMA/MAModel.cpp)

target_link_libraries(main pthread atomic)
