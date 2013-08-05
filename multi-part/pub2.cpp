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
#include <multipart.h>

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
        std::vector< std::vector< char > > msgs;
        std::vector< char > msg1((char*) &pid, ((char*) &pid) + sizeof(pid));
        char h[] = "hello";
        std::vector< char > msg2(h, h + strlen(h));
        msgs.push_back(msg1);
        msgs.push_back(msg2);
    	send_messages(req, msgs);
        sleep(2);
    }
    rc = zmq_close(req);
    assert(rc == 0);
    rc = zmq_ctx_destroy(ctx);
    assert(rc == 0);
    return 0;
}

