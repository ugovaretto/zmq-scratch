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
#include <future>
#include <deque>
#include <vector>
#include <cstdio>
#include <chrono>
#include <random>
#include <cassert>
#include <algorithm>
#include <sstream>
#include <cstring>

#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

#include <multipart.h>

const int NBR_CLIENTS = 10;
const int NBR_WORKERS = 3;
const char WORKER_READY[] = "--READY";      //  Signals worker is ready

//------------------------------------------------------------------------------
void sleep(int s) {
    std::this_thread::sleep_for(std::chrono::seconds(s));
}
//------------------------------------------------------------------------------
int rand(int lower, int upper) {
    std::default_random_engine rng(std::random_device{}()); 
    std::uniform_int_distribution<int> dist(lower, upper);
    return dist(rng);  
}
//------------------------------------------------------------------------------
std::string make_id(const std::string& id, int num) {
    std::ostringstream oss;
    oss << id << ":" << num;
    return oss.str();
}
//------------------------------------------------------------------------------
void client_task(const std::string& id, //identity of peer attached through
                                        //local frontend
                 int num) { //client number
    void* context = zmq_ctx_new();
    assert(context);
    void* client  = zmq_socket(context, ZMQ_REQ);
    assert(client);
    const std::string identity(make_id(id, num));
    zmq_setsockopt(client, ZMQ_IDENTITY, identity.c_str(), identity.size());
    assert(zmq_connect(client, id.c_str()) == 0);
    std::ostringstream oss;
    while (true) {
        //  Send request, get reply
        assert(zmq_send(client, "HELLO", strlen("HELLO"), 0) > 0);
        auto send_time = std::chrono::steady_clock::now();
        char reply[0x100];
        const int rc = zmq_recv(client, reply, 0x100, 0);
        auto recv_time = std::chrono::steady_clock::now();
        auto diff = recv_time - send_time;
        oss << std::chrono::duration_cast< 
                    std::chrono::milliseconds >(diff).count()
            << " ms" << std::endl; 
        if(rc < 0) break;
        reply[rc] = '\0';
        printf("Client %d: %s - %s\n", num, reply, oss.str().c_str());
        oss.str("");
        sleep(1);
    }
    assert(zmq_close(client) == 0);
    assert(zmq_ctx_destroy(context) == 0);
}
//------------------------------------------------------------------------------
void worker_task(const std::string& id, //identity of peer attached through
                                        //local back end
                   int num) { //worker number
    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* worker = zmq_socket(ctx, ZMQ_REQ);
    assert(worker);
    const std::string identity(make_id(id, num));
    zmq_setsockopt(worker, ZMQ_IDENTITY, identity.c_str(), identity.size());
    assert(zmq_connect(worker, id.c_str()) == 0);
    //  Tell broker we're ready for work
    assert(zmq_send(worker, WORKER_READY, strlen(WORKER_READY), 0) > 0);
    CharArrays msgs;
    //  Process messages as they arrive
    std::ostringstream oss;
    oss << "Hi from server " << num << " @ " << identity;
    const std::string msg = oss.str();
    while (true) {
        msgs = std::move(recv_messages(worker));
        if(msgs.size() == 0) break;
        // printf("\n\n=========================\nWorker:\n");
        // std::cout << msgs;
        // printf("-------------------------\n\n");
        msgs.resize(msgs.size() - 1);
        msgs.push_back(std::vector< char >(msg.begin(), msg.end()));
        send_messages(worker, msgs);
    }
    assert(zmq_close(worker) == 0);
    assert(zmq_ctx_destroy(ctx) == 0);
}
//------------------------------------------------------------------------------
bool is_peer_name(const char* id, char** argv, int sz) {
    for(int i = 0; i != sz; ++i) {
        if(strcmp(id, argv[i]) == 0) return true;
    }
    return false;
}
//------------------------------------------------------------------------------
int main (int argc, char *argv []) {
    //  First argument is this broker's name
    //  Other arguments are our peers' names
    //
    if(argc < 2) {
        printf ("syntax: peering me {you}...\n");
        return 0;
    }
    const std::string self = argv[1];
    std::cout << "I: preparing broker at " << self << std::endl;
    void* ctx = zmq_ctx_new();
    assert(ctx);

    //  Bind cloud frontend to endpoint
    void* cloudfe = zmq_socket(ctx, ZMQ_ROUTER);
    assert(cloudfe);
    assert(zmq_setsockopt(cloudfe, ZMQ_IDENTITY, self.c_str(), self.size())
           == 0);
    assert(zmq_bind(cloudfe, ("ipc://" + self + "-cloud.ipc").c_str()) == 0);

    //  Connect cloud backend to all peers' frontends
    void* cloudbe = zmq_socket(ctx, ZMQ_ROUTER);
    assert(cloudbe);
    assert(zmq_setsockopt(cloudbe, ZMQ_IDENTITY, self.c_str(), self.size())
           == 0);
    for(int argn = 2; argn < argc; argn++) {
        std::string peer = argv[argn];
        std::cout << "I: connecting to cloud frontend at '" 
                  << peer << "'\n";
        assert(zmq_connect(cloudbe, ("ipc://" + peer + "-cloud.ipc").c_str())
               == 0);
    }
    // Create input notification socket to receive 'ready' signals from
    // peers and connect to each peer's notification socket to notify
    // when ready
    const std::string ready_URI = "ipc://" + self + "-cloud-ready.ipc";
    void* ready_socket_rep = zmq_socket(ctx, ZMQ_REP);
    assert(ready_socket_rep);
    assert(zmq_bind(ready_socket_rep, ready_URI.c_str()) == 0);
    std::vector< void* > peer_ready_sockets;
    for(int argn = 2; argn < argc; argn++) {
        std::string peer = argv[argn];
        std::cout << "I: connecting to notification socket '" 
                  << peer << "'\n"; 
        void* ready_socket_req = zmq_socket(ctx, ZMQ_REQ);
        assert(ready_socket_req);
        assert(zmq_connect(ready_socket_req, 
               ("ipc://" + peer + "-cloud-ready.ipc").c_str()) == 0);
        peer_ready_sockets.push_back(ready_socket_req);
    }

    //  Prepare local frontend and backend
    const std::string local_fe_URI = "ipc://" + self + "-localfe.ipc";
    const std::string local_be_URI = "ipc://" + self + "-localbe.ipc";
    void* localfe = zmq_socket(ctx, ZMQ_ROUTER);
    assert(localfe);
    assert(zmq_bind(localfe, local_fe_URI.c_str()) == 0);
    void* localbe = zmq_socket(ctx, ZMQ_ROUTER);
    assert(localbe);
    assert(zmq_bind(localbe, local_be_URI.c_str()) == 0);

    // Start workers and clients
    typedef std::vector< std::future< void > > FutureArray;
    FutureArray clients;
    FutureArray workers;

    for(int worker_nbr = 0; worker_nbr != NBR_WORKERS; ++worker_nbr)
        workers.push_back(
            std::move(
                std::async(std::launch::async,
                           worker_task, local_be_URI, worker_nbr + 1)));

   
    for(int client_nbr = 0; client_nbr != NBR_CLIENTS; ++client_nbr)
         clients.push_back(
            std::move(
                std::async(std::launch::async,
                           client_task, local_fe_URI, client_nbr + 1)));
    
    // To make sure all peers are started a REQ socket is used to notify
    // and a REP socket to receive notifications
    // Notify other peers we are ready
    std::for_each(peer_ready_sockets.begin(),
                  peer_ready_sockets.end(),
                  [](void* s) {
                      assert(zmq_send(s, 0, 0, 0) == 0); // send NOTIFY
                  });               
    // Wait for each other peer to notify and reply back
    int count = argc - 2;
    //wait for all other peers to register and reply back
    while(count) {
        //receive FROM NOTIFY
        //OTHER peer notifies
        assert(zmq_recv(ready_socket_rep, 0, 0, 0) == 0); 
        //reply to NOTIFY request 
        assert(zmq_send(ready_socket_rep, 0, 0, 0) == 0);
        --count;
    }
    //receive replies from all other peers 
    std::for_each(peer_ready_sockets.begin(),
                  peer_ready_sockets.end(),
                  [](void* s) {
                      // receive response to  NOTIFY
                      // THIS peer receives notification ACK
                      assert(zmq_recv(s, 0, 0, 0) == 0); 
                  });               

    //  Here, we handle the request-reply flow. We're using load-balancing
    //  to poll workers at all times, and clients only when there are one 
    //  or more workers available.

    std::deque< std::string > worker_queue;
    //offsets of id and data in for ROUTER offsets 
    enum {ROUTER_SOCKET_ID_OFFSET = 0, ROUTER_SOCKET_DATA_OFFSET = 1};
    enum {REQ_SOCKET_ID_OFFSET = 0,
          REQ_SOCKET_EMPTY_OFFSET = 0,
          REQ_SOCKET_DATA_OFFSET};  
    while (true) {
        // First, route any waiting replies from workers
        zmq_pollitem_t backends [] = {
            { localbe, 0, ZMQ_POLLIN, 0 },
            { cloudbe, 0, ZMQ_POLLIN, 0 }
        };
        //  If we have no workers, wait indefinitely
        int rc = zmq_poll (backends, 2,
            worker_queue.size() ? 1000: -1);
        if (rc == -1)
            break;              //  Interrupted
        std::vector< char > msg(0x200);
        CharArrays msgs;
        void* dest_socket = 0;
        //  Handle reply from local worker
        if(backends[0].revents & ZMQ_POLLIN) {
            //recv ID
            msgs = std::move(recv_messages(localbe));
            //  Interrupted
            if(msgs.size() == 0)
                break;
            const std::string wid(msgs.front().begin(),  
                                      msgs.front().end());
            worker_queue.push_back(wid);
            //need to use std::equal instead of == operator because
            //WORKER_READY is null terminated
            if(std::equal(msgs.back().begin(),
                          msgs.back().end(),
                          WORKER_READY)) {
                msgs.clear();
            } else {
                //strip REQ envelpe: id + empty delimiter
                msgs = CharArrays(++++msgs.begin(), msgs.end()); 
                if(is_peer_name(chars_to_string(msgs.front()).c_str(),
                                argv + 2, argc - 2)) {
                    dest_socket = cloudfe;
                } else {   
                    dest_socket = localfe;
                }
            } //  Or handle reply from peer broker
            //strip worker id and empty delimiter from message
        } else if(backends[1].revents & ZMQ_POLLIN) {
            msgs = std::move(recv_messages(cloudbe));
            //strip ROUTER envelope: id
            msgs = CharArrays(++msgs.begin(), msgs.end());                          
            if(is_peer_name(
                 chars_to_string
                    (msgs.front()).c_str(),
                     argv + 2,
                     argc - 2)) {
                dest_socket = cloudfe;
            } else {   
                dest_socket = localfe;
            }
        }
        //  Route reply to client if we still need to
        if(msgs.size()) {       
            send_messages(dest_socket, msgs);
        }
        while(worker_queue.size()) {
            zmq_pollitem_t frontends [] = {
                { localfe, 0, ZMQ_POLLIN, 0 },
                { cloudfe, 0, ZMQ_POLLIN, 0 }
            };
            rc = zmq_poll(frontends, 2, 0);     
            assert(rc >= 0);
            int reroutable = 0;
            //  We'll do peer brokers first, to prevent starvation
            if (frontends[1].revents & ZMQ_POLLIN) {
                msgs = std::move(recv_messages(cloudfe));
                reroutable = 0;
            } else if (frontends [0].revents & ZMQ_POLLIN) {
                msgs = std::move(recv_messages(localfe));
                reroutable = 1;
            }
            else
                break; //  No work, go back to backends
            
            //message is re-routable if received from local fe
 
            //  If reroutable, send to cloud 25% of the time
            if (reroutable && argc > 2 && rand(1, 4) == 1) {
                //  Route to random broker peer
                const int peer = rand(2, argc - 1); 
                push_front(msgs,
                           std::vector< char >(argv[peer], 
                                               argv[peer] 
                                               + strlen(argv[peer])));
                send_messages(cloudbe, msgs);
            }
            else {
                std::string worker = std::move(worker_queue.front());
                worker_queue.pop_front();
                push_front(msgs, std::vector< char >()); //SENDING TO REQ, 
                                                   //wrap with empty data
                push_front(msgs, std::vector< char >(worker.begin(), 
                                                     worker.end()));
                send_messages(localbe, msgs);
            }
        }
    }
    std::for_each(peer_ready_sockets.begin(),
                  peer_ready_sockets.end(),
                  [](void* s) {
                    assert(zmq_close(s) == 0);  
                  });
    assert(zmq_close(localbe) == 0);
    assert(zmq_close(cloudbe) == 0);
    assert(zmq_close(localfe) == 0);
    assert(zmq_close(cloudfe) == 0);
    assert(zmq_ctx_destroy(ctx) == 0);
    return 0;
}
