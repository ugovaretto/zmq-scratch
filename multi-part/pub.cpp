//Remote logger server: logs messages by publishing its process id and
//the message content; remote clients subscribe to log output through a
//broker
//Author: Ugo Varetto

//Note: UNIX only; for windows use DWORD type instead of pid_t and
//GetProcessId instead of getpid

#include <cassert>
#include <cstring>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
//for framework builds on Mac OS:
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else 
#include <zmq.h>
#endif

typedef pid_t PID;

//------------------------------------------------------------------------------
PID get_proc_id() {
    return getpid();
}

//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    if(argc < 2) {
        std::cout << "usage: " 
                  << argv[0] 
                  << " <broker URI>"
                  << std::endl;
        std::cout << "Example: logger \"tcp://logbroker:5555\"\n";          
        return 0;          
    }
    void* ctx = zmq_ctx_new(); 
    void* req = zmq_socket(ctx, ZMQ_PUB);
    const char* brokerURI = argv[1];
    int rc = zmq_connect(req, brokerURI);
    assert(rc == 0);
    unsigned char buffer[0x100];
    size_t size = 0;
    std::cout << "PID: " << get_proc_id() << std::endl;
    int pid = int(get_proc_id());
    while(1) {
        zmq_send(req, &pid, sizeof(pid), ZMQ_SNDMORE);
    	zmq_send(req, "hello", strlen("hello"), 0);
    	sleep(1);
    }
    rc = zmq_close(req);
    assert(rc == 0);
    rc = zmq_ctx_destroy(ctx);
    assert(rc == 0);
    return 0;
}

