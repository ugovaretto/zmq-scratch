#include <cassert>
#include <iostream>
#include <string>
#include <cstdlib>

#include <zmq.h>

using namespace std;

int main(int argc, char** argv) {
    if(argc < 3) {
	cerr << "usage: " << argv[0] << "<server ip address> <port>" << endl;
	return EXIT_FAILURE;
    }
    const string remoteAddress = "tcp://" + string(argv[1]) + ":" + string(argv[2]);
    cout << "Connecting to hello world server " << remoteAddress << "…" << endl;
    void* context = zmq_ctx_new();
    void* requester = zmq_socket(context, ZMQ_REQ);
    zmq_connect(requester, remoteAddress.c_str());
    int request_nbr;
    for (request_nbr = 0; request_nbr != 10; request_nbr++) {
	char buffer [10];
	cout << "Sending Hello " << request_nbr << "…" << endl;
	zmq_send(requester, "Hello", 5, 0);
	zmq_recv(requester, buffer, 10, 0);
	cout << "Received World " << request_nbr << endl;
    }
    zmq_close(requester);
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;	
}
