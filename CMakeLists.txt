cmake_minimum_required(VERSION 3.5)
project(zmq_scratch)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

#add_executable(serializer_test zcf/SerializerTest.cpp)
include_directories(/usr/local/include)
link_directories(/usr/local/lib)
link_libraries(zmq)
add_executable(rpcscratch zcf/RMIScratch.cpp)

