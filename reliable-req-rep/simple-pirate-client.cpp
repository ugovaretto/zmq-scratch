//Simple pirate client implementation from ZGuide ch. 4
//Author: Ugo Varetto
//Updated to set the socket identity

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
void Client(const char* uri, int id) {
    assert(id != 0);
    const int MAX_RETRIES = 5;
    const int REQUEST_TIMEOUT = 2500; //ms
    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* socket = zmq_socket(ctx, ZMQ_REQ);
    assert(socket);
    const int LINGER_PERIOD = 0; //discard all pending messages on close
    assert(zmq_setsockopt(socket, ZMQ_LINGER, &LINGER_PERIOD,
                          sizeof(LINGER_PERIOD)) == 0);
    assert(zmq_setsockopt(socket, ZMQ_IDENTITY, &id, sizeof(id)) == 0);
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
        std::cout << argv[0] << " <client id> <address>" << std::endl;
        return 0;
    }
    Client(argv[2], atoi(argv[1]));
    return 0;
}