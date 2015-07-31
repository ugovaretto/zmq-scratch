#include <cassert>
#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <vector>

#include <zmq.h>

using namespace std;

int main(int argc, char** argv) {
    if(argc < 3) {
        cerr << "usage: " << argv[0] << "<port> <filename>" << endl;
        return EXIT_FAILURE;
    }
    const string bindAddress = "tcp://*:" + string(argv[2]);
    ofstream os(argv[2], ios::out | ios::binary);
    if(!os) {
        cerr << "Cannot open file " << argv[3] << endl;
        return EXIT_FAILURE;
    }
    void* context = zmq_ctx_new();
    void* responder = zmq_socket(context, ZMQ_REP);
    zmq_bind(responder, bindAddress.c_str());
    size_t fileSize = 0;
    int numChunks = 0;
    zmq_recv(responder, (char*) &fileSize, sizeof(fileSize), ZMQ_RCVMORE);
    zmq_recv(responder, (char*) &numChunks, sizeof(numChunks), ZMQ_RCVMORE);
    vector< char > buffer(fileSize / numChunks, int());
    for(int i = 0; i != fileSize / numChunks; ++i) {
        zmq_recv(responder, &buffer[0], buffer.size(), ZMQ_RCVMORE);
        os.write(&buffer[0], buffer.size());
    }
    if(fileSize % numChunks != 0) {
	zmq_recv(responder, &buffer[0], fileSize % numChunks, 0);
	os.write(&buffer[0], fileSize % numChunks);
    }
    zmq_close(responder);
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;        
}
