#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>

#include <zmq.h>

using namespace std;

int main(int, char**) {
    //  Socket to talk to clients
    void* context = zmq_ctx_new();
    void* responder = zmq_socket(context, ZMQ_REP);
    int rc = zmq_bind(responder, "tcp://*:5555");
    assert(rc == 0);

    while(true) {
        char buffer[10];
        zmq_recv(responder, buffer, 10, 0);
        cout << "Received Hello" << endl;
	this_thread::sleep_for(chrono::seconds(1));          //  Do some 'work'
        zmq_send(responder, "World", 5, 0);
    }
    return 0;
}
