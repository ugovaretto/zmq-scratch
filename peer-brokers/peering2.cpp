//  Broker peering simulation (part 2)
//  Prototypes the request-reply flow

#include <iostream>
#include <string>
#include <future>
#include <deque>
#include <vector>
#include <cstdio>
#include <unistd.h>
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

#define NBR_CLIENTS 10
#define NBR_WORKERS 3
#define WORKER_READY   "\001"      //  Signals worker is ready

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
    void* client  = zmq_socket(context, ZMQ_REQ);
    const std::string identity(make_id(id, num));
    zmq_setsockopt(client, ZMQ_IDENTITY, identity.c_str(), identity.size());
    zmq_connect(client, id.c_str());
    while (true) {
        //  Send request, get reply
        zmq_send(client, "HELLO", strlen("HELLO"), 0);
        char reply[0x100];
        const int rc = zmq_recv(client, reply, 0x100, 0);
        if(rc < 0) break;
        reply[rc] = '\0';
        printf("Client: %s\n", reply);
        sleep (1);
    }
    zmq_close(client);
    zmq_ctx_destroy(context);
}
//------------------------------------------------------------------------------
void worker_task(const std::string& id, //identity of peer attached through
                                        //local back end
                   int num) { //worker number
    void* ctx = zmq_ctx_new();
    void* worker = zmq_socket(ctx, ZMQ_REQ);
    zmq_connect(worker, id.c_str());

    //  Tell broker we're ready for work
    zmq_send(worker, WORKER_READY, strlen(WORKER_READY), 0);
    std::vector< std::vector< char > > msgs;
    //  Process messages as they arrive
    while (true) {
        msgs = std::move(recv_messages(worker));
        if(msgs.size() == 0) break;
        printf("\n\n=========================\nWorker:\n");
        std::for_each(msgs.begin(), msgs.end(),
            [](const std::vector< char >& msg) {
                if(msg.size() == 0) printf("<EMPTY>\n");
                else {
                    const std::string str(msg.begin(), msg.end());
                    printf(">%s", str.c_str());
                }
            });
        send_messages(worker, msgs);
    }
    zmq_close(worker);
    zmq_ctx_destroy(ctx);
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

    //  Bind cloud frontend to endpoint
    void* cloudfe = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_setsockopt(cloudfe, ZMQ_IDENTITY, self.c_str(), self.size());
    zmq_bind(cloudfe, ("ipc://" + self + "-cloud.ipc").c_str());

    //  Connect cloud backend to all peers
    void* cloudbe = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_setsockopt(cloudbe, ZMQ_IDENTITY, self.c_str(), self.size());
    for(int argn = 2; argn < argc; argn++) {
        std::string peer = argv[argn];
        std::cout << "I: connecting to cloud frontend at '" 
                  << peer << "'\n";
        zmq_connect(cloudbe, ("ipc://" + peer + "-cloud.ipc").c_str());
    }

    //  Prepare local frontend and backend
    void* localfe = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_bind(localfe, ("ipc://" + self + "-localfe.ipc").c_str());
    void* localbe = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_bind(localbe, ("ipc://" + self + "-localbe.ipc").c_str());

    //  Get user to tell us when we can start...
    std::cout << "Press Enter when all brokers are started" << std::endl;
    const int ready = std::cin.get();

    typedef std::vector< std::future< void > > FutureArray;
    FutureArray clients;
    FutureArray workers;
    //  Start local workers
    for(int worker_nbr = 0; worker_nbr != NBR_WORKERS; ++worker_nbr)
        workers.push_back(
            std::move(
                std::async(std::launch::async,
                           worker_task, self, worker_nbr + 1)));

   
    for(int client_nbr = 0; client_nbr != NBR_CLIENTS; ++client_nbr)
         clients.push_back(
            std::move(
                std::async(std::launch::async,
                           client_task, self, client_nbr + 1)));

    //  .split request-reply handling
    //  Here, we handle the request-reply flow. We're using load-balancing
    //  to poll workers at all times, and clients only when there are one 
    //  or more workers available.

    std::deque< std::string > worker_queue;
    enum {ID, DATA};
    while (true) {
        //  First, route any waiting replies from workers
        zmq_pollitem_t backends [] = {
            { localbe, 0, ZMQ_POLLIN, 0 },
            { cloudbe, 0, ZMQ_POLLIN, 0 }
        };
        //  If we have no workers, wait indefinitely
        int rc = zmq_poll (backends, 2,
            worker_queue.size() ? 100: -1);
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
            if(std::equal(msgs[ID].begin(), msgs[ID].end(), WORKER_READY)) {
                worker_queue.push_back(std::string(msgs[ID].begin(),  
                                                   msgs[ID].end()));
            } else {
                if(is_peer_name(chars_to_string(msgs[DATA + ID]).c_str(),
                                argv + 2, argc - 2)) {
                    dest_socket = cloudfe;
                } else {   
                    dest_socket = localfe;
                }
            } //  Or handle reply from peer broker
        }  else if(backends[1].revents & ZMQ_POLLIN) {
            if(is_peer_name(&msg[0], argv + 2, argc - 2)) {
                dest_socket = cloudfe;
            } else {   
                dest_socket = localfe;
            }
        }
        //  Route reply to client if we still need to
        if(msgs.size())
            send_messages(dest_socket, CharArrays(msgs.begin() + DATA + ID,
                                                  msgs.end()));

        //  .split route client requests
        //  Now we route as many client requests as we have worker capacity
        //  for. We may reroute requests from our local frontend, but not from 
        //  the cloud frontend. We reroute randomly now, just to test things
        //  out. In the next version, we'll do this properly by calculating
        //  cloud capacity:
        while(worker_queue.size()) {
            zmq_pollitem_t frontends [] = {
                { localfe, 0, ZMQ_POLLIN, 0 },
                { cloudfe, 0, ZMQ_POLLIN, 0 }
            };
            rc = zmq_poll (frontends, 2, 0);
            assert (rc >= 0);
            int reroutable = 0;
            //  We'll do peer brokers first, to prevent starvation
            if (frontends[1].revents & ZMQ_POLLIN) {
                msgs = std::move(recv_messages(cloudfe));
                reroutable = 0;
            }
            else if (frontends [0].revents & ZMQ_POLLIN) {
                msgs = std::move(recv_messages(localfe));
                reroutable = 1;
            }
            else
                break;      //  No work, go back to backends
            
            //message is re-routable if received from local fe
 
            //  If reroutable, send to cloud 20% of the time
            //  Here we'd normally use cloud status information
            //
            if (reroutable && argc > 2) {// && randof (5) == 0) {
                //  Route to random broker peer
                int peer = 1;//randof (argc - 2) + 2;
                zmq_send(cloudbe, argv[peer], strlen(argv[peer]), ZMQ_SNDMORE);
                msgs.push_front(std::vector< char >(argv[peer], 
                                            argv[peer] + strlen(argv[peer])));
                send_messages(cloudbe, msgs);
            }
            else {
                std::string worker = std::move(worker_queue.front());
                worker_queue.pop_front();
                msgs.push_front(std::vector< char >()); //SENDING TO REQ, 
                                                        //wrap with empty data
                msgs.push_front(std::vector< char >(worker.begin(), 
                                                    worker.end()));
                send_messages(localbe, msgs);
            }
        }
    }
    zmq_close(localbe);
    zmq_close(cloudbe);
    zmq_close(localfe);
    zmq_close(cloudfe);
    return EXIT_SUCCESS;
}
