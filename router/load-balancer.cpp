//Load balancer example using C++11 threads and zeromq
//Each client sends a string and workers reply with the reversed string;
//Load balancing is obtained through a middle layer implemented with two
//ROUTER sockets
//AUTHOR: UGO VARETTO
//On Apple: clang++ -std=c++11 -stdlib=libc++ -framework ZeroMQ
#include <thread> //C++11
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <deque>
#include <cstdio>
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

//------------------------------------------------------------------------------
static const char* FRONTEND_URI = "ipc://frontend.ipc";
static const char* BACKEND_URI  = "ipc://backend.ipc";
static const int WORKER_READY = 0;

//------------------------------------------------------------------------------
class Client {
public:    
    Client(int id, const std::string& text) : 
        id_(id), text_(text), context_(nullptr), socket_(nullptr),
        buffer_(0x100, 0) {
        //initilize context and set REQ identifier to id
        context_ = zmq_ctx_new();
        socket_ = zmq_socket(context_, ZMQ_REQ);
        zmq_setsockopt(socket_, ZMQ_IDENTITY, &id_, sizeof(id_));
        zmq_connect(socket_, FRONTEND_URI);
        buffer_ = std::vector< std::string::value_type >(text_.begin(), 
                                                         text_.end());  
    }
    void operator()() const {
        //run req-repl loop
        while(true) {
            int rc = zmq_send(socket_, &buffer_[0], text_.length(), 0);
            rc = zmq_recv(socket_, &buffer_[0], buffer_.size(), 0);
            printf("%d: %s", id_, &buffer_[0]);        
        }
    }
private:
    int id_;
    std::string text_;
    void* context_;
    void* socket_;
    mutable std::vector< char > buffer_;
};
//------------------------------------------------------------------------------
class Worker {
public:    
    Worker(int id) : 
        id_(id), context_(nullptr), socket_(nullptr),
        buffer_(0x100, 0) {
        //initilize context and set REQ identifier to id
        context_ = zmq_ctx_new();
        socket_ = zmq_socket(context_, ZMQ_REQ);
        zmq_setsockopt(socket_, ZMQ_IDENTITY, &id_, sizeof(id_));
        zmq_connect(socket_, BACKEND_URI);
        zmq_send(socket_, &WORKER_READY, sizeof(WORKER_READY), 0);
    }
    void operator()() const {
        int client_id = -1;
        char empty = char(0);
        while(true) {
            std::fill(buffer_.begin(), buffer_.end(), char(0));
            zmq_recv(socket_, &client_id, sizeof(client_id), 0);
            std::cout << "**" << std::endl;
            zmq_recv(socket_, &empty, sizeof(empty), 0);
            zmq_recv(socket_, &buffer_[0], buffer_.size(), 0);
            const std::string txt(&buffer_[0]);
            std::copy(txt.rbegin(), txt.rend(), buffer_.begin());
            zmq_send(socket_, &client_id, sizeof(client_id), ZMQ_SNDMORE);
            zmq_send(socket_, &empty, sizeof(empty), ZMQ_SNDMORE);
            zmq_send(socket_, &buffer_[0], txt.size(), 0);
        }
    }
private:
    int id_;
    void* context_;
    void* socket_;
    mutable std::vector< char > buffer_;
};
//------------------------------------------------------------------------------
int main(int argc, char** argv) {
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
        workers.push_back(std::thread(Worker(i + 1)));
    }
    char empty = char(0);
    int worker_id = -1;
    int client_id = -1;
    int rc = -1;
    std::vector< char > request(0x100, 0);
    std::vector< char > reply(0x100, 0);
    while(true) {
        zmq_pollitem_t items[] = {
            {backend, 0, ZMQ_POLLIN, 0},
            {frontend, 0, ZMQ_POLLIN, 0}};
        rc = zmq_poll(items, worker_queue.size() > 0 ? 2 : 1, -1);
        if(rc == -1) break;
        if(items[0].revents & ZMQ_POLLIN) {
            zmq_recv(backend, &worker_id, sizeof(worker_id), 0);
            worker_queue.push_back(worker_id);
            zmq_recv(backend, &empty, sizeof(empty), 0);
            zmq_recv(backend, &client_id, sizeof(client_id), 0);
            if(client_id != WORKER_READY) {
                zmq_recv(backend, &empty, sizeof(empty), 0);
                rc = zmq_recv(backend, &reply[0], reply.size(), 0);
                zmq_send(frontend, &client_id, sizeof(client_id), ZMQ_SNDMORE);
                zmq_send(frontend, &empty, sizeof(empty), ZMQ_SNDMORE);
                zmq_send(frontend, &reply[0], rc, 0);
            }
        }
        if(items[1].revents & ZMQ_POLLIN) {
            zmq_recv(frontend, &client_id, sizeof(client_id), 0);
            zmq_recv(frontend, &empty, sizeof(empty), 0);
            rc = zmq_recv(frontend, &request[0], request.size(), 0);
            worker_id = worker_queue.front();
            zmq_send(backend, &worker_id, sizeof(worker_id), ZMQ_SNDMORE);
            zmq_send(backend, &empty, sizeof(empty), ZMQ_SNDMORE);
            zmq_send(backend, &client_id, sizeof(client_id), ZMQ_SNDMORE);
            zmq_send(backend, &empty, sizeof(empty), ZMQ_SNDMORE);
            zmq_send(backend, &request[0], rc, 0);
            worker_queue.pop_front();
        }     
    }
    zmq_close(frontend);
    zmq_close(backend);
    zmq_ctx_destroy(context);
    return 0;
}
