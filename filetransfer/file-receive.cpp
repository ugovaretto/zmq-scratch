#include <cassert>
#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <vector>

#include <zmq.h>

using namespace std;

int main(int argc, char** argv) {
    if(argc < 4) {
        cerr << "usage: " << argv[0] << " <server ip address> <port> "
                "<filename>" << endl;
        cerr << "if server address is \"*\" (with quotes) then "
                "it starts as a server, client otherwise" << endl;
        return EXIT_FAILURE;
    }
    const string address = "tcp://" + string(argv[1])
                               + ":" + string(argv[2]);
    const bool SERVER = string(argv[1]) == "*";
    ofstream os(argv[3], ios::out | ios::binary);
    if(!os) {
        cerr << "Cannot open file " << argv[3] << endl;
        return EXIT_FAILURE;
    }
    //clog << address << endl;
    void* context = zmq_ctx_new();
    void* responder = zmq_socket(context, ZMQ_REP);
    if(SERVER) zmq_bind(responder, address.c_str());
    else zmq_connect(responder, address.c_str());
    size_t fileSize = 0;
    size_t chunkSize = 0;
    zmq_recv(responder, (char*) &fileSize, sizeof(fileSize), 0);
    //clog << fileSize << endl;
    zmq_send(responder, 0, 0, 0);
    zmq_recv(responder, (char*) &chunkSize, sizeof(chunkSize), 0);
    zmq_send(responder, 0, 0, 0);
    vector< char > buffer(chunkSize, char());
    const int numChunks = int(fileSize / chunkSize); 
    //clog << "Buffer size: " << buffer.size() << endl;
    for(int i = 0; i != numChunks; ++i) {
        zmq_recv(responder, &buffer[0], buffer.size(), 0);
        zmq_send(responder, 0, 0, 0);
        //clog << "chunk received" << endl;
        os.write(&buffer[0], buffer.size());
    }
    if(fileSize % chunkSize != 0) {
        zmq_recv(responder, &buffer[0], fileSize % chunkSize, 0);
        zmq_send(responder, 0, 0, 0);
        //clog << "chunk received" << endl;
        os.write(&buffer[0], fileSize % chunkSize);
    }
    zmq_close(responder);
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;        
}
