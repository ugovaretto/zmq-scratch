//Load balanced broker from Ch3/4 ZGuide, added support for <seq id, payload>
//message format; in a real-world scenario use the functions in multipart.h
//to avoid dealing with the message format detail.
//Author: Ugo Varetto
//use with *lazy* pirate client and *simple* pirate worker

#include <iostream>
#include <vector>
#include <algorithm>
#include <set> //we have a timestamp in the record data' now
               //data is automatically sorted by timestamp
               //from highest to lowest
#include <chrono>
#include <cassert>
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

namespace {
const int WORKER_READY = 123;
const int HEARTBEAT = 111;

typedef std::chrono::time_point< std::chrono::steady_clock > timepoint;
typedef std::chrono::duration< long int > duration;

const duration EXPIRATION_INTERVAL = 
    std::chrono::duration_cast< duration >(
        std::chrono::milliseconds(15 * 1000));
const duration HEARTBEAT_INTERVAL =
    std::chrono::duration_cast< duration >(
        std::chrono::milliseconds(1 * 1000));
const int TIMEOUT = 1 * 1000;             
}

//------------------------------------------------------------------------------
class worker_info {
public:    
    bool operator >(const worker_info& wi) const {
        return timestamp_ > wi.timestamp_;
    }
    operator int() const { return id_; }
    worker_info(int id = -1) : 
        id_(id),
        timestamp_(std::chrono::steady_clock::now()) {}
    const timepoint& timestamp() const { return timestamp_; }
    int id() const { return id_; }     
private:
    int id_;
    timepoint timestamp_;    
};

typedef std::set< worker_info > Workers;  

//------------------------------------------------------------------------------
//elements are ordered from highest to lowest
//1) find the first element which has a time > expiration time
//2) remove all elements from that element to last element is set
void purge(Workers& workers, const duration& cutoff) {
    typedef Workers::iterator WI;
    WI start =  std::find_if(
                    workers.begin(),
                    workers.end(),
                    [&cutoff](const worker_info& wi) { 
                    return 
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now()
                            - wi.timestamp()
                            ) > cutoff;
                        });
    if(start == workers.end()) return;
    workers.erase(start, workers.end());
               
}
//------------------------------------------------------------------------------
//if worker already present remove it and re-insert it in the right
//position
void push(Workers& workers, int id) {

    Workers::iterator it = std::find_if(workers.begin(),
                               workers.end(),
                               [id](const worker_info& wi){
                                   return wi.id() == id;
                               });
    if(it != workers.end()) workers.erase(it);
    
    workers.insert(worker_info(id));
}
//------------------------------------------------------------------------------
int pop(Workers& workers) { 
    assert(workers.size() > 0);
    std::set< worker_info >::iterator back = --workers.end();
    const int ret = back->id();
    workers.erase(back);
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
  
    //create communication objects   	
    void* context = zmq_ctx_new();
    assert(context);
    void* frontend = zmq_socket(context, ZMQ_ROUTER);
    assert(frontend);
    void* backend = zmq_socket(context, ZMQ_ROUTER);
    assert(backend);
    assert(zmq_bind(frontend, FRONTEND_URI) == 0);
    assert(zmq_bind(backend, BACKEND_URI) == 0);

    //workers ordered queue 
    Workers workers;
c 
    int worker_id = -1;
    int client_id = -1;
    int rc = -1;
    std::vector< char > request(0x100, 0);
    std::vector< char > reply(0x100, 0);
    int serviced_requests = 0;
    //loop until max requests servided
    while(serviced_requests < MAX_REQUESTS) {
        zmq_pollitem_t items[] = {
            {backend, 0, ZMQ_POLLIN, 0},
            {frontend, 0, ZMQ_POLLIN, 0}};
        //remove all workers that have not been active for a
        //time > expiration interval
        //XXX TODO: TEST    
        purge(workers, EXPIRATION_INTERVAL);        
        //XXX    
        //poll for incoming requests: if no workers are available
        //only poll for workers(backend) since there is no point
        //in trying to service a client request without active
        //workers
        rc = zmq_poll(items, workers.size() > 0 ? 2 : 1,
                      TIMEOUT);
        if(rc == -1) break;
        //data from workers
        if(items[0].revents & ZMQ_POLLIN) {    
            assert(zmq_recv(backend, &worker_id, sizeof(worker_id), 0) > 0);
            assert(zmq_recv(backend, 0, 0, 0) == 0);       
            assert(zmq_recv(backend, &client_id, sizeof(client_id), 0) > 0);
            //add worker to list of available workers
            push(workers, worker_id);
            assert(workers.size() > 0);
            //of not a 'ready' message forward message to frontend
            //workers send 'ready' messages when either 
            if(client_id != WORKER_READY) {
                int seq_id = -1;
                assert(zmq_recv(backend, 0, 0, 0) == 0);
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
        //request from clients
        if(items[1].revents & ZMQ_POLLIN) { 
            int seq_id = -1;
            //receive request |client id|<null>|request id|data|
            zmq_recv(frontend, &client_id, sizeof(client_id), 0);
            zmq_recv(frontend, 0, 0, 0);
            rc = zmq_recv(frontend, &seq_id, sizeof(seq_id), 0);
            assert(rc > 0);
            const int req_size = zmq_recv(frontend, &request[0],
                                          request.size(), 0);
            assert(req_size > 0);
            //take worker from list and forward request to it
            worker_id = pop(workers);
            assert(worker_id > 0);
            zmq_send(backend, &worker_id, sizeof(worker_id), ZMQ_SNDMORE);
            zmq_send(backend, 0, 0, ZMQ_SNDMORE);
            zmq_send(backend, &client_id, sizeof(client_id), ZMQ_SNDMORE);
            zmq_send(backend, 0, 0, ZMQ_SNDMORE);
            zmq_send(backend, &seq_id, sizeof(seq_id), ZMQ_SNDMORE);
            zmq_send(backend, &request[0], req_size, 0);         
        } 
        const int hb = HEARTBEAT; //capturing HEARTBEAT directly generates
                                  //a warning because the lambda function should
                                  //not capture a variable with non-automatic
                                  //storage
        //send heartbeat request to all workers: workers reply to such request
        //with a 'ready' message
        std::for_each(workers.begin(),
                      workers.end(),
                      [backend, hb](const worker_info& wi) {
                          const int id = wi.id();
                          zmq_send(backend, &id,
                                   sizeof(id), ZMQ_SNDMORE);
                          zmq_send(backend, 0, 0, ZMQ_SNDMORE);
                          zmq_send(backend, &hb, sizeof(hb), 0);            
                      });
        

    }
    zmq_close(frontend);
    zmq_close(backend);
    zmq_ctx_destroy(context);
    return 0;
}
