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
#include <future>

#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

static const int WORKER_READY = 123;

//------------------------------------------------------------------------------
void sleep(int s) {
    std::this_thread::sleep_for(std::chrono::seconds(s));
}

//------------------------------------------------------------------------------
void Worker(const char* uri) {

    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* socket = zmq_socket(ctx, ZMQ_REQ);
    assert(socket);
    const int LINGER_PERIOD = 0; //discard all pending messages on close
    assert(zmq_setsockopt(socket, ZMQ_LINGER, &LINGER_PERIOD,
                          sizeof(LINGER_PERIOD)) == 0);
    assert(zmq_connect(socket, uri) == 0);
    assert(zmq_send(socket, &WORKER_READY, sizeof(WORKER_READY), 0) > 0);
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
int main(int argc, char** argv) {
    if(argc < 3) {
        std::cout << argv[0] 
                  << " <number of workers> <broker address>" << std::endl;
        return 0;
    }
    const int NUM_WORKERS = atoi(argv[1]);
    assert(NUM_WORKERS > 0);
    // Start workers and clients
    typedef std::vector< std::future< void > > FutureArray;
    FutureArray workers;
    for(int t = 0; t != NUM_WORKERS; ++t) {
        workers.push_back(
                    std::move(
                        std::async(std::launch::async, Worker, argv[2])));
    }
    return 0;
}