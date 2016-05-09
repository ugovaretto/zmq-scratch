/*
 *  * router-dealer.cpp
 *
 *  Created on: Aug 7, 2015
 *      Author: ugo
 */



//Async client server: client sends async requests through a DEALER socket
//and polls replies
//ROUTER socket receives the complete envelope [socket identity][message]
//and dispatches it to the DEALER as a multipart message
//Note that no empty delimiter is added to the envelope by the DEALER; in
//order to make it compliant with REQ/REP socket an empty field must
//be manually added

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <random>
//#ifdef __APPLE__
//#include <ZeroMQ/zmq.h>
//#else
#include <zmq.h>
//#endif

//------------------------------------------------------------------------------
//  This is our client task
//  It connects to the server, and then sends a request once per second
//  It collects responses as they arrive, and it prints them out. We will
//  run several client tasks in parallel, each with a different random ID.

void client_task() {
    void* ctx = zmq_ctx_new();
    void* client = zmq_socket(ctx, ZMQ_REQ);

    //  Set random identity to make tracing easier
    std::ostringstream ts;
    int id = 10;
    zmq_setsockopt(client, ZMQ_IDENTITY, (char*) &id, sizeof(id));
    zmq_connect(client, "ipc://router-dealer");
    std::cout << "CONNECTED" << std::endl;
    int request_nbr = 0;
    std::vector< char > buffer(0x1000, char(0));
    std::ostringstream oss;
    //while(true) {
    {
        oss.str("");
        oss << "request " << ++request_nbr;
        zmq_send(client, oss.str().c_str(), oss.str().size(), 0);
        const int rc = zmq_recv(client, &buffer[0], buffer.size(), 0);
        buffer[rc] = '\0';
        std::cout << id << " -> " << &buffer[0];
    }
    zmq_close(client);
    zmq_ctx_destroy(ctx);
}


//------------------------------------------------------------------------------
//  Each worker task works on one request at a time and sends a random number
//  of replies back, with random delays between replies:
void server_task() {
    void* ctx = zmq_ctx_new();
    void *worker = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_bind(worker, "ipc://router-dealer");
    std::vector< char > buffer(0x1000, char(0));
    std::ostringstream oss;
    //while(true) {
    {// The ROUTER socket connected to the DEALER socket gives us the
        // reply envelope and message
        int id = 0;
        zmq_recv(worker, (char*) &id, sizeof(int), 0);

        std::cout << "ID: " << id << std::endl;
        const int rc = zmq_recv(worker, &buffer[0], buffer.size(), 0);
        buffer[rc] = '\0';
        zmq_send(worker, &id, sizeof(id), ZMQ_SNDMORE);
        zmq_send(worker, 0, 0, ZMQ_SNDMORE);
        oss.str("");
        oss << " - " << id << ": " << &buffer[0] << " - SERVICED";
        zmq_send(worker, oss.str().c_str(), oss.str().size(), 0);
    }
    zmq_close(worker);
}

//------------------------------------------------------------------------------
void quiet_termination() { exit(0); }

//  The main thread simply starts several clients and a server, and then
//  waits for the server to finish.

//------------------------------------------------------------------------------
int main (void)
{
    std::set_terminate(quiet_termination);
    new std::thread(client_task);
    new std::thread(server_task);

//    new std::thread(client_task);
//    new std::thread(client_task);

    while(true);
    return 0;
}





