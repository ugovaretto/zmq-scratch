//Remote logger client: subscribe to specific process ids to receive
//log messages.
//Author: Ugo Varetto

//Note: UNIX only; for windows use DWORD type instead of pid_t

#include <cassert>
#include <iostream>
#include <vector>
#include <vector>
#include <chrono>
#include <thread>

#include <zmq.h>

//------------------------------------------------------------------------------
using namespace std;
int main(int argc, char** argv) {
    if(argc < 2) {
        std::cout << "usage: " 
                  << argv[0] 
                  << " <publisher URI> [message size default=1MB]"
                  << std::endl;
        return 0;          
    }
    void* ctx = zmq_ctx_new(); 
    void* publisher = zmq_socket(ctx, ZMQ_SUB);
    const char* URI = argv[1];
    int rc = zmq_connect(publisher, URI);
    assert(rc == 0);
    zmq_setsockopt(publisher, ZMQ_SUBSCRIBE, "", 0);
    bool hex = argc == 3 ? find(argv[2], argv[2] + strlen(argv[2]), 'x')
                           != argv[2] + strlen(argv[2]) : false;
    int base = hex ? 16 : 10;
    const int MESSAGE_SIZE = argc == 3 ? stoi(argv[2], 0, base) : 0x100000;
    const int NUM_MESSAGES = 1000;
    const int ONE_MB = 0x100000;
    vector< char > buffer(MESSAGE_SIZE);
    while(1) {
        const chrono::time_point< chrono::steady_clock > start =
            chrono::steady_clock::now();
        int i = 0;
        while(i != NUM_MESSAGES) {
            rc = zmq_recv(publisher, buffer.data(), buffer.size(), ZMQ_NOBLOCK);
            if(rc == buffer.size()) ++i;
            else this_thread::sleep_for(chrono::milliseconds(4));
        }
//        for(int i = 0; i != NUM_MESSAGES; ++i) {
//            rc = zmq_recv(publisher, buffer.data(), buffer.size(), ZMQ_NOBLOCK);
//            cout << rc << ' ';
//            //assert(rc == buffer.size());
//        }
        const chrono::time_point< chrono::steady_clock > end =
                chrono::steady_clock::now();
        const chrono::duration< double, ratio<1, 1> > d = end - start;
        cout << "Bandwidth: "
             << ((MESSAGE_SIZE * NUM_MESSAGES) / ONE_MB)
                / d.count() << " MB/s"
             << endl;
    }
    rc = zmq_close(publisher);
    assert(rc == 0);
    rc = zmq_ctx_destroy(ctx);
    assert(rc == 0);
    return 0;
}

