cmake_minimum_required(VERSION 3.23)
project(ARIMA)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(ARIMA
        ARMAMath.cpp
        MAModel.cpp
        main.cpp
       )
