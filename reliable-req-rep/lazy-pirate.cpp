//Lazy pirate implementation from ZGuide ch. 4
//Author: Ugo Varetto
//Changes to original C code:
// - C++11, chrono & random
// - sending both a sequence number and a payload
// - used clock guided simulation instead of simple counter
// - probability distribution: 60% regular, 30% overload, 10% crash  

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <cassert>
#include <cstring>
#include <thread>

#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

//------------------------------------------------------------------------------
void sleep(int s) {
    std::this_thread::sleep_for(std::chrono::seconds(s));
}

//------------------------------------------------------------------------------
void Server(const char* uri) {

    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* socket = zmq_socket(ctx, ZMQ_REP);
    assert(socket);
    const int LINGER_PERIOD = 0; //discard all pending messages on close
    assert(zmq_setsockopt(socket, ZMQ_LINGER, &LINGER_PERIOD,
                          sizeof(LINGER_PERIOD)) == 0);
    assert(zmq_bind(socket, uri) == 0);
    std::vector< char > buffer(0x100);
    int sequence = -1;
    std::default_random_engine rng(std::random_device{}()); 
    std::uniform_int_distribution<int> dist(1, 100);
    const int NINETY_PERCENT = 90;
    const int THIRTY_PERCENT = 30;
    assert(sizeof(long int) >= 8);
    const std::chrono::duration<long int> GUARANTEED_UP_TIME =
                                                    std::chrono::seconds(15);
    const auto start = std::chrono::steady_clock::now();                                                
    while(true) {
        int rc = zmq_recv(socket, &sequence, sizeof(sequence), 0);
        assert(rc > 0);
        assert(sequence >= 0);
        const int buffer_size = zmq_recv(socket, &buffer[0], buffer.size(), 0);
        assert(buffer_size > 0 && buffer_size <= buffer.size());
        const auto elapsed_time = std::chrono::steady_clock::now() - start;
        //20% probability of crashing after guaranteed uptime
        if(elapsed_time > GUARANTEED_UP_TIME) {
            //10% probability of crashing
            if(dist(rng) > NINETY_PERCENT) {
                std::cout << ">CRASHING" << std::endl;
                break;
            //30% probabilty of server overload    
            } else if(dist(rng) <= THIRTY_PERCENT) {
                std::cout << ">OVERLOAD" << std::endl;
                sleep(3); 
            }
        }
        rc = zmq_send(socket, &sequence, sizeof(sequence), ZMQ_SNDMORE);
        assert(rc > 0);
        rc = zmq_send(socket, &buffer[0], buffer_size, 0);
        assert(rc > 0);
        sequence = -1;
    }
    assert(zmq_close(socket) == 0);
    assert(zmq_ctx_destroy(ctx) == 0); 
}

//------------------------------------------------------------------------------
void Client(const char* uri) {
    const int MAX_RETRIES = 5;
    const int REQUEST_TIMEOUT = 2500; //ms
    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* socket = zmq_socket(ctx, ZMQ_REQ);
    assert(socket);
    const int LINGER_PERIOD = 0; //discard all pending messages on close
    assert(zmq_setsockopt(socket, ZMQ_LINGER, &LINGER_PERIOD,
                          sizeof(LINGER_PERIOD)) == 0);
    assert(zmq_connect(socket, uri) == 0);
    int sequence = 0;
    int retries = MAX_RETRIES;
    std::vector< char > buffer(0x100);
    while(retries > 0) {
        int rc = zmq_send(socket, &sequence, sizeof(sequence), ZMQ_SNDMORE);
        assert(rc >0);
        rc = zmq_send(socket, "REQUEST", strlen("REQUEST"), 0);
        assert(rc > 0);
        while(true) {
            zmq_pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
            int rc = zmq_poll(items, 1, REQUEST_TIMEOUT);
            assert(rc >=0);
            int recv_sequence = -1;
            if(items[0].revents & ZMQ_POLLIN) {
                rc = zmq_recv(socket, &recv_sequence, sizeof(recv_sequence), 0);
                assert(rc > 0);
                rc = zmq_recv(socket, &buffer[0], buffer.size(), 0);
                assert(rc > 0);
                if(recv_sequence == sequence)
                    std::cout << ">REPLY RECEIVED" << std::endl;
                else 
                    std::cout << ">ERROR: MALFORMED REPLY RECEIVED"
                              << std::endl;
                ++sequence;
                retries = MAX_RETRIES;
                sleep(1);
                break;              
            } else {
                if(--retries == 0) {
                    std::cout << ">SERVER NOT RESPONDING" << std::endl;
                    break;
                } else {
                    assert(zmq_close(socket) == 0);
                    socket = zmq_socket(ctx, ZMQ_REQ);
                    assert(socket);
                    assert(zmq_setsockopt(socket, ZMQ_LINGER, &LINGER_PERIOD,
                          sizeof(LINGER_PERIOD)) == 0);
                    assert(zmq_connect(socket, uri) == 0);
                    items[0].socket = socket;
                    int rc = zmq_send(socket, &sequence, sizeof(sequence),
                                      ZMQ_SNDMORE);
                    assert(rc>0);
                    rc = zmq_send(socket, "REQUEST", strlen("REQUEST"), 0);
                    assert(rc > 0);
                }
            }
        }
    }
    assert(zmq_close(socket) == 0);
    assert(zmq_ctx_destroy(ctx) == 0);
}
 
//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    if(argc < 3) {
        std::cout << argv[0] << " <client|server> <address>" << std::endl;
        return 0;
    }
    if(std::string(argv[1]) == "client") Client(argv[2]);
    else if(std::string(argv[1]) == "server") Server(argv[2]);
    else {
        std::cerr << "Unknown parameter '" << argv[1] << "'" << std::endl;
        return 1;
    }
    return 0;
}