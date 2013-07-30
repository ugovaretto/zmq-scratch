//Remote logger client: subscribe to specific process ids to receive
//log messages.
//Author: Ugo Varetto

//Note: UNIX only; for windows use DWORD type instead of pid_t

#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <vector>
#include <cstdlib>
//for framework builds on Mac OS:
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else 
#include <zmq.h>
#endif

typedef int PID;

//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    if(argc < 2) {
        std::cout << "usage: " 
                  << argv[0] 
                  << " <server URI> [process id]"
                  << std::endl;
        std::cout << "Example: client \"tcp://logbroker:5555\" 27852\n";
        std::cout << "To receive notifications from ALL processes omit the"
                     " process id parameter\n";          
        return 0;          
    }
    void* ctx = zmq_ctx_new(); 
    void* publisher = zmq_socket(ctx, ZMQ_SUB);
    const char* brokerURI = argv[1];
    const PID pid = argc > 2 ? atoi(argv[2]) : 0;
    int rc = zmq_connect(publisher, brokerURI);
    assert(rc == 0);
    rc = pid > 0 ? zmq_setsockopt(publisher, ZMQ_SUBSCRIBE, &pid, sizeof(pid))
                 : zmq_setsockopt(publisher, ZMQ_SUBSCRIBE, "", 0); 
    assert(rc == 0);
    unsigned char buffer[0x100];
    int p = -1;
    while(1) {
        rc = zmq_recv(publisher, &p, sizeof(p), 0);
        assert(rc > 0);
        std::cout << p << std::endl;
        rc = zmq_recv(publisher, &buffer[0], 0x100, 0);
        buffer[rc] = '\0';
        std::cout << buffer << std::endl;
        //break;
    }
    rc = zmq_close(publisher);
    assert(rc == 0);
    rc = zmq_ctx_destroy(ctx);
    assert(rc == 0);
    return 0;
}

