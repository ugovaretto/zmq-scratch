cmake_minimum_required(VERSION 3.5)
project(zmq_scratch)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

#add_executable(serializer_test zcf/SerializerTest.cpp)
include_directories(/opt/local/include)
link_directories(/opt/local/lib)
link_libraries(zmq)
add_executable(rpcscratch zcf/RMIScratch.cpp)
link_libraries(sodium)
add_executable(sodium-test encryption/sodium-test.cpp)
add_executable(cryptoclient encryption/crypto-client.cpp)
add_executable(cryptoserver encryption/crypto-server.cpp)
