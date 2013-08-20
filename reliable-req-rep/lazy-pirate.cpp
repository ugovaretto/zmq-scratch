//---------------------
// Multi-broker example (reworked example from ZGuide - Ch. 3) 
//---------------------
// Author: Ugo Varetto
//---------------------
// Each broker is attached to a number of "local" clients and workers through
// REQ, REP sockets as well as other peer brokers through ROUTER sockets.
// "local" in this case means that the clients and workers are started as 
// separate threads by the process that runs the broker
// Data flow:
// - client send data to local front-end
// - broker receives data from through local and peer front-ends
// - if all local workers are busy data is routed to peer-brokers through
//   cloud back-end, if not it is routed to local backend   
//
// Initialization:
// Each broker creates ang binds the required sockets then connects
// the backend to each other peer's frontend.
//
// To make sure all peers are available to exchange messages an handshaking
// process takes pace after initialization:
// 1. broker sends READY signal to all other peers on a REQ socket
// 2. broker receives on a REP socket a number of notification messages equal to
//    the number of peers - 1 (itself), and replies to each message with an ACK
// 3. broker receives ACK from each peer on the REQ socket that started the
//    handshaking process
//
// run in separate terminals as e.g. peer 1 2 3, peer 2 3 1, peer 3 1 2

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <cassert>
#include <cstring>

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
int rand(int lower, int upper) {
    
    return dist(rng);  
}
//------------------------------------------------------------------------------
void Server(const char* uri) {

    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* socket = zmq_socket(ctx, ZMQ_REP);
    assert(socket);
    assert(zmq_bind(socket, uri) == 0);
    int cycles = 0;
    std::vector< char > buffer(0x100);
    int sequence = -1;
    std::default_random_engine rng(std::random_device{}()); 
    std::uniform_int_distribution<int> dist(1, 5);
    assert(sizeof(long int) >= 8);
    const std::chrono::duration<long int> GUARANTEED_UP_TIME =
                                                    std::chrono::seconds(15);
    const auto start = std::chrono::steady_clock::now();                                                
    while(true) {
        int rc = zmq_recv(socket, &sequence, sizeof(sequence), 0);
        assert(rc > 0);
        assert(sequence > 0);
        const int buffer_size = zmq_recv(socket, &buffer[0], buffer.size(), 0);
        assert(buffer_size > 0 && buffer_size <= buffer.size());
        auto elapsed_time = std::chrono::steady_clock::now() - start;
        const int rnd = dist(rng);
        //20% probability of crashing after guaranteed uptime
        if(elapsed_time > GUARANTEED_UP_TIME && rnd == 1) {
            std::cout << ">CRASHING" << std::endl;
            break;
        //20% probaility of overload    
        } else if(rnd == 1)  {
            std::cout << ">OVERLOAD - ";
            sleep(3);
        }
        rc = zmq_send(socket, &sequence, sizeof(sequence), ZMQ_SNDMORE);
        assert(rc > 0);
        rc = zmq_send(socket, &buffer[0], buffer_size, 0);
        assert(rc > 0);
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
            if(items[0].revents && ZMQ_POLLIN) {
                rc = zmq_recv(socket, &recv_sequence, sizeof(recv_sequence), 0);
                assert(rc > 0);
                rc = zmq_recv(socket, &buffer[0], buffer.size(), 0);
                assert(rc > 0);
                if(recv_sequence == sequence)
                    std::cout << ">REPLY RECEIVED" << std::endl;
                else 
                    std::cout << "> ERROR: MALFORMED REPLY RECEIVED"
                              << std::endl;
                ++sequence;
                break;              
            } else {
                if(--retries == 0) {
                    std::cout << ">SERVER NOT RESPONDING" << std::endl;
                    break;
                } else {
                    assert(zmq_close(socket) == 0);
                    socket = zmq_socket(ctx, ZMQ_REQ);
                    int rc = zmq_send(socket, &sequence, sizeof(sequence), ZMQ_SNDMORE);
                    assert(rc >0);
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
    if(std::string(argv[1]) == "client") Client();
    else if(std::string(argv[2]) == "server") Server();
    else {
        std::cerr << "Unknown parameter '" << argv[1] << "'" << std::endl;
        return 1;
    }
    return 0;
}