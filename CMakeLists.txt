cmake_minimum_required(VERSION 3.5)
project(zmq_scratch)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

add_executable(serializer_test zcf/SerializerTest.cpp)

#set(SOURCE_FILES main.cpp)
#add_executable(zmq_scratch ${SOURCE_FILES})