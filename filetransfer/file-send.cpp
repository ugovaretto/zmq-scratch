#include <cassert>
#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <limits>
#include <algorithm>

#include <zmq.h>

using namespace std;

size_t FileSize(ifstream& is) {
  const streampos pos = is.tellg();
  is.clear();
  is.seekg(is.beg);
  is.ignore(numeric_limits<streamsize>::max());
  const size_t size = is.gcount();
  is.clear(); //clear EOF (set by ignore)
  is.seekg(pos);
  return size;
}

int main(int argc, char** argv) {
    if(argc < 5) {
        cerr << "usage: " << argv[0] << " <server ip address> <port> "
                "<filename> <chunk size>" << endl;
        return EXIT_FAILURE;
    }
    const string remoteAddress = "tcp://" + string(argv[1])
                                 + ":" + string(argv[2]);
    ifstream is(argv[3], ios::in | ios::binary);
    if(!is) {
        cerr << "Cannot open file " << argv[3] << endl;
        return EXIT_FAILURE;
    }
    const size_t fsize = FileSize(is);
    assert(fsize > 0);
    char* pEnd = nullptr;
    const size_t chunkSize = min(fsize, size_t(strtoull(argv[4], &pEnd, 10)));
    assert(chunkSize > 0);
    std::vector< char > buffer(chunkSize);
    
    void* context = zmq_ctx_new();
    void* requester = zmq_socket(context, ZMQ_REQ);
    zmq_connect(requester, remoteAddress.c_str());
    const int numChunks = fsize / chunkSize;
    zmq_send(requester, (char*) &fsize, sizeof(fsize), 0);
    zmq_recv(requester, 0, 0, 0);
    zmq_send(requester, (char*) &chunkSize, sizeof(numChunks), 0);
    zmq_recv(requester, 0, 0, 0);
    clog << "Buffer size: " << buffer.size() << endl;
    for(int i = 0; i != numChunks; ++i) {
        is.read(&buffer[0], buffer.size());
        zmq_send(requester, &buffer[0], buffer.size(), 0);
        zmq_recv(requester, 0, 0, 0);
    }
    if(fsize % chunkSize != 0) {
        is.read(&buffer[0], (long int)(fsize % chunkSize));
        zmq_send(requester, &buffer[0], fsize % chunkSize, 0);
        zmq_recv(requester, 0, 0, 0);
    }
    zmq_close(requester);
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;	
}
