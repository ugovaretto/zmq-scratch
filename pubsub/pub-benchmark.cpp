//Remote logger server: logs messages by publishing its process id and
//the message content; remote clients subscribe to log output through a
//broker
//Author: Ugo Varetto

//Note: UNIX only; for windows use DWORD type instead of pid_t and
//GetProcessId instead of getpid

#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>

#include <zmq.h>

using namespace std;


//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    if(argc < 2) {
        std::cout << "usage: " 
                  << argv[0] 
                  << " <URI> [message size default=1MB]"
                  << std::endl;
        return 0;          
    }
    void* ctx = zmq_ctx_new(); 
    void* pub = zmq_socket(ctx, ZMQ_PUB);
    const char* URI = argv[1];
    int rc = zmq_bind(pub, URI);
    assert(rc == 0);
    bool hex = argc == 3 ? find(argv[2], argv[2] + strlen(argv[2]), 'x')
               != argv[2] + strlen(argv[2]) : false;
    int base = hex ? 16 : 10;
    const int MESSAGE_SIZE = argc == 3 ? stoi(argv[2], 0, base) : 0x100000;
    vector< char > buffer(MESSAGE_SIZE);
    while(1) {
    	zmq_send(pub, buffer.data(), buffer.size(), 0);
        this_thread::sleep_for(chrono::microseconds(100));
    }
    rc = zmq_close(pub);
    assert(rc == 0);
    rc = zmq_ctx_destroy(ctx);
    assert(rc == 0);
    return 0;
}

