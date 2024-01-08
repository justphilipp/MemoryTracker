# 指定可执行文件的生成位置
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
aux_source_directory(. TEST_LIST)
# cmake三部曲
# 指定头文件-指定库文件-链接
# 指定头文件目录
#include_directories(${PROJECT_SOURCE_DIR}/ext/gtest)
# 指定静态链接库文件目录
#link_directories(${PROJECT_SOURCE_DIR}/ext/gtest/lib)
# 执行文件
add_executable(${PROJECT_NAME} ${TEST_LIST})
target_link_libraries(${PROJECT_NAME} gtest gtest_main)