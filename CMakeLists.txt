cmake_minimum_required(VERSION 3.10)
project(AsyncLogger)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加可执行文件
add_executable(async_logger
    src/main.cpp
    src/Logger.cpp
    src/AsyncLogger.cpp
    src/SocketServer.cpp
)

# 链接线程库
target_link_libraries(async_logger pthread)

# 启用调试信息（便于调试）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall")
