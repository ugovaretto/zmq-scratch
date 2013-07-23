//Async client server: client sends async requests through a DEALER socket
//and polls replies
//ROUTER socket receives the complete envelope [socket identity][message]
//and dispatches it to the DEALER as a multipart message
//Note that no empty delimiter is added to the envelope by the DEALER; in
//order to make it compliant with REQ/REP socket an empty field must
//be manually added

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

//------------------------------------------------------------------------------
//  This is our client task
//  It connects to the server, and then sends a request once per second
//  It collects responses as they arrive, and it prints them out. We will
//  run several client tasks in parallel, each with a different random ID.

void client_task() {
    void* ctx = zmq_ctx_new();
    void* client = zmq_socket(ctx, ZMQ_DEALER);

    //  Set random identity to make tracing easier
    std::ostringstream ts;
    ts << std::this_thread::get_id();
    const std::string id = ts.str();
    zmq_setsockopt(client, ZMQ_IDENTITY, id.c_str(), id.size());
    zmq_connect(client, "tcp://localhost:5570");

    zmq_pollitem_t items[] = { { client, 0, ZMQ_POLLIN, 0 } };
    int request_nbr = 0;
    std::vector< char > buffer(0x100, char(0));
    std::ostringstream oss;
    while(true) {
        //  Tick once per second, pulling in arriving messages
        int centitick;
        for(centitick = 0; centitick < 100; centitick++) {
            zmq_poll(items, 1, 10);
            if(items[0].revents & ZMQ_POLLIN) {
                const int rc = zmq_recv(client, &buffer[0], buffer.size(), 0);
                buffer[rc] = '\0';
                printf("%s : %s\n", id.c_str(), &buffer[0]);
            }
        }
        oss.str("");
        oss << "request " << ++request_nbr;
        zmq_send(client, oss.str().c_str(), oss.str().size(), 0);
    }
    zmq_close(client);
    zmq_ctx_destroy(ctx);
}

//------------------------------------------------------------------------------
//  .split server task
//  This is our server task.
//  It uses the multithreaded server model to deal requests out to a pool
//  of workers and route replies back to clients. One worker can handle
//  one request at a time but one client can talk to multiple workers at
//  once.

static void server_worker (void *ctx);

void server_task() {
    //  Frontend socket talks to clients over TCP
    void* ctx = zmq_ctx_new();
    void *frontend = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_bind(frontend, "tcp://*:5570");

    //  Backend socket talks to workers over inproc
    void *backend = zmq_socket(ctx, ZMQ_DEALER);
    zmq_bind(backend, "inproc://backend");

    //  Launch pool of worker threads, precise number is not critical
    int thread_nbr;
    for (thread_nbr = 0; thread_nbr < 5; thread_nbr++)
        new std::thread(server_worker, ctx);

    //  Connect backend to frontend via a proxy
    zmq_proxy(frontend, backend, 0);

    zmq_close(frontend);
    zmq_close(backend);
    zmq_ctx_destroy(ctx);
}

//------------------------------------------------------------------------------
//  .split worker task
//  Each worker task works on one request at a time and sends a random number
//  of replies back, with random delays between replies:
void server_worker(void* ctx) {
    void *worker = zmq_socket(ctx, ZMQ_DEALER);
    zmq_connect(worker, "inproc://backend");
    std::vector< char > id(0x100, char(0));
    std::vector< char > buffer(0x100, char(0));
    std::ostringstream oss;
    while(true) {
        // The ROUTER socket connected to the DEALER socket gives us the
        // reply envelope and message
        const int idrc = zmq_recv(worker, &id[0], id.size(), 0);
        id[idrc] = '\0';
        const int rc = zmq_recv(worker, &buffer[0], buffer.size(), 0);
        buffer[rc] = '\0';
        const int replies = 2;//= randof (5);
        for (int reply = 0; reply < replies; reply++) {
            //  Sleep for some fraction of a second
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            zmq_send(worker, &id[0], idrc, ZMQ_SNDMORE);
            oss.str("");
            oss << " - " << &id[0] << ": " << &buffer[0] << " - SERVICED";
            zmq_send(worker, oss.str().c_str(), oss.str().size(), 0);
        }   
    }
    zmq_close(worker);
}

//------------------------------------------------------------------------------
void quiet_termination() { exit(0); }

//  The main thread simply starts several clients and a server, and then
//  waits for the server to finish.

//------------------------------------------------------------------------------
int main (void)
{
    std::set_terminate(quiet_termination);
    new std::thread(client_task);
    new std::thread(client_task);
    new std::thread(client_task);
    new std::thread(server_task);
    std::chrono::milliseconds duration(5000);
    std::this_thread::sleep_for(duration);
    return 0;
}
