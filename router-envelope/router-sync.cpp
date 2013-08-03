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
#ifdef USE_IPC
const char* syncURI = "ipc://sync.ipc";
const char* uri = "ipc://router.ipc";
#else
const char* syncURI = "tcp://0.0.0.0:7777";
const char* uri = "tcp://0.0.0.0:8888";
#endif
void WaitOn(void* socket) {
    assert(zmq_recv(socket, 0, 0, 0) == 0);
    assert(zmq_send(socket, 0, 0, 0) == 0);
}

void Notify(void* socket) {
    assert(zmq_send(socket, 0, 0, 0) == 0);
    assert(zmq_recv(socket, 0, 0, 0) == 0);
}

//------------------------------------------------------------------------------
void Server() {
    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* socket = zmq_socket(ctx, ZMQ_ROUTER);
    assert(socket);
    assert(zmq_setsockopt(socket, ZMQ_IDENTITY, &serverid, sizeof(serverid))
           == 0);
    assert(zmq_bind(socket, uri) == 0);
    void* syncSocket = zmq_socket(ctx, ZMQ_REP);
    assert(zmq_bind(syncSocket, syncURI) == 0);
    assert(syncSocket);
    WaitOn(syncSocket);
    zmq_send(socket, &clientid, sizeof(clientid), ZMQ_SNDMORE);
    const char msg[] = "Hello from a ROUTER!";
    zmq_send(socket, msg, strlen(msg), 0);
#ifdef USE_IPC    
    int id = -1;
    zmq_recv(socket, &id, sizeof(id), 0);
    zmq_recv(socket, 0, 0, 0);
#else //WARNING: MacOS X 10.8.4: when using IPC the following line
      //or anything that prints to stdout causes client and server to hang
    std::cout << "Sent " << "'" << msg << "'" << std::endl;
#endif         
    printf("$\n");
    assert(zmq_close(socket) == 0);
    assert(zmq_close(syncSocket) == 0);
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
    void* syncSocket = zmq_socket(ctx, ZMQ_REQ);
    assert(syncSocket);
    assert(zmq_connect(syncSocket, syncURI) == 0);
    Notify(syncSocket);
    int id = -1;
    //receive id of remote socket
    int rc = zmq_recv(socket, &id, sizeof(id), 0);
    //receive payload
    char buf[0x100];
    rc = zmq_recv(socket, buf, 0x100, 0);
    assert(rc > 0 );
#ifdef USE_IPC    
    zmq_send(socket, &serverid, sizeof(serverid), ZMQ_SNDMORE);
    zmq_send(socket, 0, 0, 0);
#endif
    std::cout << "Received " << buf << " from " << id << std::endl;    
    buf[rc] = '\0';  
    assert(zmq_close(socket) == 0);
    assert(zmq_close(syncSocket) == 0);
    assert(zmq_ctx_destroy(ctx) == 0); 
 
}
//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    if(argc > 1) Server();
    else Client();
    return 0;
}

