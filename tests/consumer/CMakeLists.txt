cmake_minimum_required(VERSION 3.25)

project(celerpay_consumer LANGUAGES CXX)

find_package(celerpay CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE celerpay::celerpay)
