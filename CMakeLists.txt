cmake_minimum_required(VERSION 3.26)
project(MyTinyWebServer VERSION 1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#include_directories(test)

set(SOURCES config/config.h
        lock/Locker.h
        http/http_conn.h http/http_conn.cpp
        log/block_queue.h log/log.h log/log.cpp
        threadpool/ThreadPool.h
        timer/timer.cpp timer/timer.h
        )

add_link_options(-lpthread)

add_executable(MyTinyWebServer main.cpp ${SOURCES})

#add_executable(test test/test.cpp)