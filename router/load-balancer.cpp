//Load balancer example using C++11 threads and zeromq
//Each client sends a string and workers reply with the reversed string;
//Load balancing is obtained through a middle layer implemented with two
//ROUTER sockets; threads are stored into an STL collection
//AUTHOR: UGO VARETTO
//On Apple: clang++ -std=c++11 -stdlib=libc++ -framework ZeroMQ
//Todo: implement move constructors setting copied object context and socket
//to nullptr: this allows to properly destroy zeromq context and close socket
//in the destructor if not NULL
#include <thread> //C++11
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <deque>
#include <cstdio>
#include <cassert>
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

//------------------------------------------------------------------------------
static const char* FRONTEND_URI = "tcp://0.0.0.0:5555";//"ipc://frontend.ipc";
static const char* BACKEND_URI  = "tcp://0.0.0.0:5556";//"ipc://backend.ipc";
static const int WORKER_READY = 123;

//------------------------------------------------------------------------------
class Client {
public:    
    Client(int id, const std::string& text) : 
        id_(id), text_(text), context_(nullptr), socket_(nullptr),
        buffer_(0x100, 0) {
        
                                       
    }
    void operator()() const {
        //initilize context and set REQ identifier to id
        context_ = zmq_ctx_new();
        socket_ = zmq_socket(context_, ZMQ_REQ);
        zmq_setsockopt(socket_, ZMQ_IDENTITY, &id_, sizeof(id_));
        zmq_connect(socket_, FRONTEND_URI);
        buffer_ = std::vector< char >(text_.begin(), 
                                      text_.end());
        buffer_.push_back(char(0)); 
        
        int rc = zmq_send(socket_, &buffer_[0], text_.length(), 0);
        rc = zmq_recv(socket_, &buffer_[0], buffer_.size(), 0);
        buffer_[rc] = char(0);
        printf("%d: %s -> %s\n", id_, text_.c_str(), &buffer_[0]);  
    }
private:
    int id_;
    std::string text_;
    mutable void* context_;
    mutable void* socket_;
    mutable std::vector< char > buffer_;
};
//------------------------------------------------------------------------------
class Worker {
public:    
    Worker(int id) : 
        id_(id), context_(nullptr), socket_(nullptr),
        buffer_(0x100, 0), started_(false) {
        //initilize context and set REQ identifier to id
        
    }
    void operator()() const {
        started_ = true;
        context_ = zmq_ctx_new();
        socket_ = zmq_socket(context_, ZMQ_REQ);
        zmq_setsockopt(socket_, ZMQ_IDENTITY, &id_, sizeof(id_));
        zmq_connect(socket_, BACKEND_URI);
        zmq_send(socket_, &WORKER_READY, sizeof(WORKER_READY), 0);
        int client_id = -1;
        int rc = -1;
        started_ = true;
        while(true) {
            zmq_recv(socket_, &client_id, sizeof(client_id), 0);
            rc = zmq_recv(socket_, 0, 0, 0);
            //assert(rc == 0);
            rc = zmq_recv(socket_, &buffer_[0], buffer_.size(), 0);
            buffer_[rc] = char(0);
            const std::string txt(&buffer_[0]);
            std::copy(txt.rbegin(), txt.rend(), buffer_.begin());
            zmq_send(socket_, &client_id, sizeof(client_id), ZMQ_SNDMORE);
            zmq_send(socket_, 0, 0, ZMQ_SNDMORE);
            zmq_send(socket_, &buffer_[0], txt.size(), 0);
        }
    }
    ~Worker() {
        if(started_) {
            zmq_close(socket_);
            zmq_ctx_destroy(context_);
        }
    }
private:
    int id_;
    mutable void* context_;
    mutable void* socket_;
    mutable std::vector< char > buffer_;
    mutable bool started_;
};

//------------------------------------------------------------------------------
void quiet_termination() { exit(0); }

//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    //this is required because on termination the worker threads are still
    //running and a abort() will be called generating an error on termination;
    //a cleaner way is to handle termination directly in the worker threads
    //by:
    // 1) using async i/o and exit automatically if no requests are received
    //    after a specific amount of time OR
    // 2) using async i/o and checking control messages on a separate channel OR
    // 3) using async i/o and checking a condition variable set from the thread
    //    that invokes the destructor
    // 4) run indefinitely and handle SIGINT/SIGTERM
    std::set_terminate(quiet_termination);
	
    void* context = zmq_ctx_new();
    void* frontend = zmq_socket(context, ZMQ_ROUTER);
    void* backend = zmq_socket(context, ZMQ_ROUTER);
    zmq_bind(frontend, FRONTEND_URI);
    zmq_bind(backend, BACKEND_URI);
    std::string text("abcdefgh");
    
    static const int MAX_WORKERS = std::thread::hardware_concurrency();
    const int MAX_CLIENTS = argc == 1 ? 4 : atoi(argv[1]);
    std::cout << MAX_WORKERS << " workers, " << MAX_CLIENTS << " clients\n";
    std::deque< int > worker_queue;
    
    std::vector< std::thread > clients;
    std::vector< std::thread > workers;
    for(int i = 0; i != MAX_CLIENTS; ++i) {
        clients.push_back(std::thread(Client(i + 1, text)));
        std::next_permutation(text.begin(), text.end());
    }    
    for(int i = 0; i != MAX_WORKERS; ++i) {
        workers.push_back(std::thread(Worker(MAX_CLIENTS + i + 1)));
    }
    int worker_id = -1;
    int client_id = -1;
    int rc = -1;
    std::vector< char > request(0x100, 0);
    std::vector< char > reply(0x100, 0);
    int serviced_requests = 0;
    while(serviced_requests < MAX_CLIENTS) {
        zmq_pollitem_t items[] = {
            {backend, 0, ZMQ_POLLIN, 0},
            {frontend, 0, ZMQ_POLLIN, 0}};
        rc = zmq_poll(items, worker_queue.size() > 0 ? 2 : 1, -1);
        if(rc == -1) break;
        if(items[0].revents & ZMQ_POLLIN) {
            zmq_recv(backend, &worker_id, sizeof(worker_id), 0);
            worker_queue.push_back(worker_id);
            zmq_recv(backend, 0, 0, 0);
            zmq_recv(backend, &client_id, sizeof(client_id), 0);
            if(client_id != WORKER_READY) {
                zmq_recv(backend, 0, 0, 0);
                rc = zmq_recv(backend, &reply[0], reply.size(), 0);
                zmq_send(frontend, &client_id, sizeof(client_id), ZMQ_SNDMORE);
                zmq_send(frontend, 0, 0, ZMQ_SNDMORE);
                zmq_send(frontend, &reply[0], rc, 0);
                ++serviced_requests;
            } 
        }
        if(items[1].revents & ZMQ_POLLIN) {
            zmq_recv(frontend, &client_id, sizeof(client_id), 0);
            zmq_recv(frontend, 0, 0, 0);
            rc = zmq_recv(frontend, &request[0], request.size(), 0);
            worker_id = worker_queue.front();
            zmq_send(backend, &worker_id, sizeof(worker_id), ZMQ_SNDMORE);
            zmq_send(backend, 0, 0, ZMQ_SNDMORE);
            zmq_send(backend, &client_id, sizeof(client_id), ZMQ_SNDMORE);
            zmq_send(backend, 0, 0, ZMQ_SNDMORE);
            zmq_send(backend, &request[0], rc, 0);
            worker_queue.pop_front();
        }     
    }
    zmq_close(frontend);
    zmq_close(backend);
    zmq_ctx_destroy(context);
    std::terminate();
    return 0;
}
