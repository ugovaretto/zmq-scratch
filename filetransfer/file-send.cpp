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
	cerr << "usage: " << argv[0] << "<server ip address> <port> <filename>" << endl;
	return EXIT_FAILURE;
    }
    const string remoteAddress = "tcp://" + string(argv[1]) + ":" + string(argv[2]);
    ifstream is(argv[3], ios::in | ios::binary);
    if(!is) {
	cerr << "Cannot open file " << argv[3] << endl;
	return EXIT_FAILURE;
    }
    is.seekg(is.end);
    const size_t fsize = is.tellg();
    is.seekg(is.beg);
    std::vector< char > buffer(fsize);
    
    void* context = zmq_ctx_new();
    void* requester = zmq_socket(context, ZMQ_REQ);
    zmq_connect(requester, remoteAddress.c_str());
    const int numChunks = 1; //= fsize / CHUNK_SIZE
    const size_t chunkSize = buffer.size();
    zmq_send(requester, (char*) &fsize, sizeof(fsize), ZMQ_SNDMORE);
    zmq_send(requester, (char*) &numChunks, sizeof(numChunks), ZMQ_SNDMORE);
    for(int i = 0; i != numChunks; ++i) {
	is.read(&buffer[0], buffer.size()); 
	zmq_send(requester, &buffer[0], buffer.size(), ZMQ_SNDMORE);
    }

//     if(fsize % CHUNK_SIZE != 0) {
// 	is.read(&buffer[0], fsize % CHUNK_SIZE);
// 	zmq_send(requester, &buffer[0], fsize % CHUNK_SIZE, 0);
//     } else zmq_send(requester, 0, 0, 0);	
    zmq_send(requester, 0, 0, 0);
    zmq_close(requester);
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;	
}
