//
// Created by Ugo Varetto on 7/8/16.
//
#include <iostream>
#include <cstdlib>
#include <cassert>
#include <string>
#include <vector>

#include <zmq.h>

#include "../utility.h"

using namespace std;
int main(int argc, char** argv) {
    void* ctx = zmq_ctx_new();
    assert(ctx);
    void* server = zmq_socket(ctx, ZMQ_STREAM);
    assert(ctx);
    ZCheck(zmq_bind(server, "tcp://*:4444"));
    std::vector< char > buf(0x10000);
    vector< char > id(0x100);
    int idsize = zmq_recv(server, id.data(), id.size(), 0);
    int rc = zmq_recv(server, buf.data(), buf.size(), 0);
    ZCheck(rc);
    buf[rc] = '\0';
    const std::string msg(buf.data());
    std::cout << msg << std::endl;
    std::string reply = msg + " to you";
    ZCheck(zmq_send(server, id.data(), size_t(idsize), ZMQ_SNDMORE));
    ZCheck(zmq_send(server, reply.c_str(), reply.size(), 0));
    return EXIT_SUCCESS;
}