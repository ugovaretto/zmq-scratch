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
    const string bindAddress = "tcp://*:" + string(argv[1]);
    ofstream os(argv[2], ios::out | ios::binary);
    if(!os) {
        cerr << "Cannot open file " << argv[3] << endl;
        return EXIT_FAILURE;
    }
    clog << bindAddress << endl;
    void* context = zmq_ctx_new();
    void* responder = zmq_socket(context, ZMQ_REP);
    zmq_bind(responder, bindAddress.c_str());
    size_t fileSize = 0;
    size_t chunkSize = 0;
    zmq_recv(responder, (char*) &fileSize, sizeof(fileSize), 0);
    clog << fileSize << endl;
    zmq_send(responder, 0, 0, 0);
    zmq_recv(responder, (char*) &chunkSize, sizeof(chunkSize), 0);
    zmq_send(responder, 0, 0, 0);
    vector< char > buffer(chunkSize, char());
    const int numChunks = int(fileSize / chunkSize); 
    clog << "Buffer size: " << buffer.size() << endl;
    for(int i = 0; i != numChunks; ++i) {
        zmq_recv(responder, &buffer[0], buffer.size(), 0);
	zmq_send(responder, 0, 0, 0);
        clog << "chunk received" << endl;
	os.write(&buffer[0], buffer.size());
    }
    if(fileSize % chunkSize != 0) {
	zmq_recv(responder, &buffer[0], fileSize % chunkSize, 0);
	zmq_send(responder, 0, 0, 0);
	clog << "chunk received" << endl;
	os.write(&buffer[0], fileSize % chunkSize);
    }
    zmq_close(responder);
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;        
}
