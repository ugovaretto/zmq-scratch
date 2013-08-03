//ROUTER-ROUTER communication
//Author: Ugo Varetto
//start server: >a.out server, then client >a.out
//server waits for 10s before sending messages
//SERVER: send destination id + message
//CLIENT: receive server id + message
#include <cassert>
#include <unistd.h>
#include <iostream>
//for framework builds on Mac OS:
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else 
#include <zmq.h>
#endif

const int clientid = 5;
const int serverid = 555;
const char* uri = "ipc://router.ipc";

//------------------------------------------------------------------------------
void Server() {
    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* socket = zmq_socket(ctx, ZMQ_ROUTER);
    assert(socket);
    assert(zmq_setsockopt(socket, ZMQ_IDENTITY, &serverid, sizeof(serverid))
           == 0);
    assert(zmq_bind(socket, uri) == 0);
    sleep(10);
    zmq_send(socket, &clientid, sizeof(clientid), ZMQ_SNDMORE);
    const char msg[] = "Hello from a ROUTER!";
    zmq_send(socket, msg, strlen(msg), 0);
    std::cout << "Sent " << "'" << msg << "'\n";
    assert(zmq_close(socket) == 0);
    assert(zmq_ctx_destroy(ctx) == 0); 
}

//------------------------------------------------------------------------------
void Client() {
    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* socket = zmq_socket(ctx, ZMQ_ROUTER);
    assert(zmq_setsockopt(socket, ZMQ_IDENTITY, &clientid, sizeof(clientid))
             == 0);
    assert(zmq_connect(socket, uri) == 0);
    int id = -1;
    //receive id of remote socket
    int rc = zmq_recv(socket, &id, sizeof(id), 0);
    //receive payload
    char buf[0x100]; 
    rc = zmq_recv(socket, buf, 0x100, 0);
    assert(rc > 0 );
    buf[rc] = '\0';
    std::cout <<"Received " << buf << " from " << id << std::endl;
}

//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    if(argc > 1) Server();
    else Client();
    return 0;
}

