//Paranoid pirate implementation from ZGuide ch. 4
//Author: Ugo Varetto
//Changes to original C code:
// - C++11, chrono & random
// - sending both a sequence number and a payload
// - used clock guided simulation instead of simple counter
// - probability distribution: 60% regular, 30% overload, 10% crash  
//The main change in the communication pattern is the additional parsing
//of the |server id|<empty>| message headers handled automatically by the
//REQ socket

//IMPORTANT: when using DEALER sockets:
// - do not send the target id since the target is determined by the run-time
//   as done for REQ
// - do not recv the source id when connected to a router socket


#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <cassert>
#include <cstring>
#include <thread>
#include <future>
#include <algorithm>
#include <cerrno>

#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

namespace {
const int WORKER_READY = 123;
const int HEARTBEAT = 111;
}

typedef std::chrono::duration< long int > duration;

//------------------------------------------------------------------------------
void sleep(int s) {
    std::this_thread::sleep_for(std::chrono::seconds(s));
}

//------------------------------------------------------------------------------
void Worker(const char* uri, int id) {
    const duration POLL_INTERVAL =
        std::chrono::duration_cast< duration >(
            std::chrono::milliseconds(2500));
    const int MAX_LIVENESS = 3;
    const int MAX_RETRIES = 3;    
    const int BROKER_ID = 1000;
    assert(id != 0);
    void* ctx = zmq_ctx_new();
    assert(ctx);
    //changed to DEALER: need to deal with empty markers automatically
    //stripped away by REQ sockets 
    void* socket = zmq_socket(ctx, ZMQ_DEALER);
    assert(socket);
    assert(zmq_setsockopt(socket, ZMQ_IDENTITY, &id, sizeof(int)) == 0);
    const int LINGER_TIME = 0;
    assert(zmq_setsockopt(socket, ZMQ_LINGER,
                          &LINGER_TIME, sizeof(LINGER_TIME)) == 0);
    assert(zmq_connect(socket, uri) == 0);
    //other end expects a message in ZMQ_REQ/REP format: |id|<empty>|data|
    //DEALER sockets: never select the destination since it'a automatically
    //selected by run-time, different from ROUTER
    assert(zmq_send(socket, 0, 0, ZMQ_SNDMORE) == 0);
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
    //receive: |server id|<empty>|client id|<empty>|sequence id| payload|                                                
    int clientid = -1;
    int serverid = -1;
    int retries = MAX_RETRIES;       //number of reconnection attempts:
                                     //after MAX_LIVENESS poll intervals
                                     //without incoming data have passed

    int server_alive = MAX_LIVENESS; //number poll intervals without incoming
                                     //data passed before trying to reconnect 

    while(true) {
        zmq_pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
        int rc = zmq_poll(items, 1, POLL_INTERVAL.count());
        if(rc == -1) break;
        if(items[0].revents & ZMQ_POLLIN) {
            server_alive = MAX_LIVENESS;
            retries = MAX_RETRIES;
            rc = zmq_recv(socket, 0, 0, 0);
            assert(rc == 0);
            rc = zmq_recv(socket, &clientid, sizeof(clientid), 0);
            assert(rc > 0);
            //got data from broker
            if(clientid != HEARTBEAT) {
                rc = zmq_recv(socket, 0, 0, 0);
                assert(rc == 0);
                rc = zmq_recv(socket, &sequence, sizeof(sequence), 0);
                assert(rc > 0);
                assert(sequence >= 0);
                const int buffer_size = zmq_recv(socket, &buffer[0],
                                                 buffer.size(), 0);
                assert(buffer_size > 0 && buffer_size <= buffer.size());
                const auto elapsed_time = std::chrono::steady_clock::now()
                                          - start;
#ifdef SIMULATION                
                if(elapsed_time > GUARANTEED_UP_TIME) {
                    //10% probability of crashing
                    if(dist(rng) > NINETY_PERCENT) {
                        std::cout << id << ">CRASHING" << std::endl;
                        break;
                    //30% probabilty of server overload    
                    } else if(dist(rng) <= THIRTY_PERCENT) {
                        std::cout << id << ">OVERLOAD" << std::endl;
                        sleep(3); 
                    }
                }
#endif                                          
                //send empty marker (not needed for REQ sockets)
                rc = zmq_send(socket, 0, 0, ZMQ_SNDMORE);
                assert(rc == 0);
                rc = zmq_send(socket, &clientid, sizeof(clientid), ZMQ_SNDMORE);
                assert(rc > 0);
                rc = zmq_send(socket, 0, 0, ZMQ_SNDMORE);
                assert(rc == 0);
                rc = zmq_send(socket, &sequence, sizeof(sequence), ZMQ_SNDMORE);
                assert(rc > 0);
                rc = zmq_send(socket, &buffer[0], buffer_size, 0);
                assert(rc > 0);
                sequence = -1;
            }
        } else { //no data received after timeout
            //decreament alive counter; if 0 
            if(--server_alive == 0 ) {
                if(--retries == 0) break;
                sleep(2 * POLL_INTERVAL.count());
                assert(zmq_close(socket) == 0);
                socket = zmq_socket(ctx, ZMQ_DEALER);
                assert(socket);
                assert(zmq_setsockopt(socket, ZMQ_IDENTITY,
                       &id, sizeof(int)) == 0);
                assert(zmq_setsockopt(socket, ZMQ_LINGER,
                          &LINGER_TIME, sizeof(LINGER_TIME)) == 0);
                assert(zmq_connect(socket, uri) == 0);
                server_alive = MAX_LIVENESS;
            }
            //send heartbeat as WORKER_READY
            rc = zmq_send(socket, 0, 0, ZMQ_SNDMORE);
            assert(rc == 0);
            zmq_send(socket, &WORKER_READY, sizeof(WORKER_READY), 0);
        }
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
                        std::async(std::launch::async, Worker,
                                   argv[2], t + 1)));
    }
    std::for_each(workers.begin(), workers.end(),
                 [](FutureArray::value_type& f) {
                    f.wait();
                 });
    return 0;
}