//Load balanced broker from Ch3/4 ZGuide, added support for <seq id, payload>
//message format; in a real-world scenario use the functions in multipart.h
//to avoid dealing with the message format detail.
//Author: Ugo Varetto
//use with *lazy* pirate client and *simple* pirate worker

#include <iostream>
#include <vector>
#include <set> //we have a timestamp in the record data now
               //data is automatically sorted by timestamp
#include <cassert>
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

static const int WORKER_READY = 123;
static const int HEARBEAT = 111;

typedef std::chrono::time_point< std::chrono::steady_clock > timepoint;

const static std::chrono::duration< long int > EXPIRATION_INTERVAL = 
    std::chrono::milliseconds(15 * 1000);
const static std::chrono::duration< long int > HEARTBEAT_INTERVAL =
    std::chrono::milliseconds(1 * 1000);    

//------------------------------------------------------------------------------
class worker_info {
    bool operator >(const worker_info& wi) const {
        return timestamp > wi.timestamp;
    }
    operator int() const { return id_; }
    worker_info(int id = -1) : 
        id_(id),
        timestamp_(std::chrono::steady_clock::now()) {} 
private:
    int id_;
    timepoint timestamp_;    
};

typedef std::set< worker_info, std::greater< worker_info > > Workers;  

//------------------------------------------------------------------------------
void purge(Workers& workers, long int cutoff) {
    workers.erase(std::find_if(workers.rbegin(),
                               workers.rend(),
                               [cutoff](const worker_info& wi) { 
                                 return 
                                    std::chrono::duration_cast(
                                        std::chrono::steady_clock::now()
                                        - wi.timestamp
                                        ) >= EXPIRATION;
                                }), workers.rend());
}
//------------------------------------------------------------------------------
void push(Workers& workers, int id) {
    //if worker already present remove
    workers.erase(std::find_if(workers.begin(),
                               workers.end(),
                               [id](const worker_info& wi){
                                   return wi.id_ == id;
                               }));
    workers.insert(worker_info(id));
}
//------------------------------------------------------------------------------
int pop(const Workers& workers, long int cutoff) { 
    if(worker.size() == 0) return -1;
    const int ret = *workers.begin();
    worker.erase(workers.begin());
    return ret;
}
//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    if(argc < 3) {
        std::cout << "usage: "
                  << argv[0] << " <frontend address> <backend address>"
                  << std::endl;
        return 0;
    }

    const char* FRONTEND_URI = argv[1];
    const char* BACKEND_URI  = argv[2];
    const int MAX_REQUESTS = 100;
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
   	
    void* context = zmq_ctx_new();
    assert(context);
    void* frontend = zmq_socket(context, ZMQ_ROUTER);
    assert(frontend);
    void* backend = zmq_socket(context, ZMQ_ROUTER);
    assert(backend);
    const int BACKEND_ID = 1000;
    //since the worker is using a dealer socket it will parse
    //both identity and empty marker
    assert(setsockopt(socket, ZMQ_IDENTITY, 
           &BACKEND_ID, sizeof(BACKEND_ID)) == 0);
    assert(zmq_bind(frontend, FRONTEND_URI) == 0);
    assert(zmq_bind(backend, BACKEND_URI) == 0);

    std::deque< int > worker_queue;
    
    int worker_id = -1;
    int client_id = -1;
    int rc = -1;
    std::vector< char > request(0x100, 0);
    std::vector< char > reply(0x100, 0);
    int serviced_requests = 0;
    while(serviced_requests < MAX_REQUESTS) {
        zmq_pollitem_t items[] = {
            {backend, 0, ZMQ_POLLIN, 0},
            {frontend, 0, ZMQ_POLLIN, 0}};
        purge(workers, EXPIRATION_INTERVAL);        
        rc = zmq_poll(items, worker_queue.size() > 0 ? 2 : 1,
                      MAX_DURATION.count());
        if(rc == -1) break;
        if(items[0].revents & ZMQ_POLLIN) {
            zmq_recv(backend, &worker_id, sizeof(worker_id), 0);
            push(workers, worker_id);
            worker_queue.push_back(worker_id);
            zmq_recv(backend, 0, 0, 0);
            zmq_recv(backend, &client_id, sizeof(client_id), 0);
            if(client_id != WORKER_READY) {
                int seq_id = -1;
                zmq_recv(backend, 0, 0, 0);
                rc = zmq_recv(backend, &seq_id, sizeof(seq_id), 0);
                assert(rc > 0);
                rc = zmq_recv(backend, &reply[0], reply.size(), 0);
                assert(rc > 0);
                zmq_send(frontend, &client_id, sizeof(client_id), ZMQ_SNDMORE);
                zmq_send(frontend, 0, 0, ZMQ_SNDMORE);
                zmq_send(frontend, &seq_id, sizeof(seq_id), ZMQ_SNDMORE);
                zmq_send(frontend, &reply[0], rc, 0);
                ++serviced_requests;
            } 
        }
        if(items[1].revents & ZMQ_POLLIN) {      
            int seq_id = -1;
            zmq_recv(frontend, &client_id, sizeof(client_id), 0);
            zmq_recv(frontend, 0, 0, 0);
            rc = zmq_recv(frontend, &seq_id, sizeof(seq_id), 0);
            assert(rc > 0);
            rc = zmq_recv(frontend, &request[0], request.size(), 0);
            assert(rc > 0);
            worker_id = worker_queue.front();
            zmq_send(backend, &worker_id, sizeof(worker_id), ZMQ_SNDMORE);
            zmq_send(backend, 0, 0, ZMQ_SNDMORE);
            zmq_send(backend, &client_id, sizeof(client_id), ZMQ_SNDMORE);
            zmq_send(backend, 0, 0, ZMQ_SNDMORE);
            zmq_send(backend, &seq_id, sizeof(seq_id), ZMQ_SNDMORE);
            zmq_send(backend, &request[0], rc, 0);
            worker_queue.pop_front();
        }
        std::for_each(workers.begin(),
                      workers.end(),
                      [backend, HEARTBEAT](const worker_info& wi) {
                          zmq_send(backend, &wi.id,
                                   sizeof(&wi.id), ZMQ_SNDMORE);
                          zmq_send(backend, 0, 0, 0);
                          zmq_send(backend, &HEARTBEAT, sizeof(HEARBEAT), 0);            
                      });


    }
    zmq_close(frontend);
    zmq_close(backend);
    zmq_ctx_destroy(context);
    return 0;
}
