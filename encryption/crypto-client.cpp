#include <cassert>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>

#include <zmq.h>
#include <sodium.h>
#include "sodium-util.h"




using namespace std;

int main(int argc, char **argv) {
    if (argc < 3) {
        cerr << "usage: " << argv[0] << " <server ip address> <port>" << endl;
        return EXIT_FAILURE;
    }
    const string remoteAddress = "tcp://" + string(argv[1]) + ":" + string(argv[2]);
    cout << "Connecting to hello world server " << remoteAddress << "…" << endl;
    void *context = zmq_ctx_new();
    void *requester = zmq_socket(context, ZMQ_REQ);
    zmq_connect(requester, remoteAddress.c_str());

    const bool INITIATE_OPTION = true;
    SecretNonce sn = HandShake(requester, INITIATE_OPTION);
    const SharedSecret& sharedSecret = sn.sharedSecret;
    const Nonce nonce = sn.nonce;
    //5 encrypt-send / decrypt-receive loop
    int request_nbr;
    vector< unsigned char > tmpbuffer(0x1000);
    for (request_nbr = 0; request_nbr != 10; request_nbr++) {
        const vector< unsigned char >& buffer = Encrypt("Hello", 5,
                                                        sharedSecret, nonce, tmpbuffer);
        zmq_send(requester, buffer.data(), buffer.size(), 0);
        const size_t sz = zmq_recv(requester, tmpbuffer.data(), tmpbuffer.size(), 0);
        vector< unsigned char > decrypted;
        Decrypt(tmpbuffer.data(), tmpbuffer.size(), sharedSecret, nonce, decrypted);
        cout << "Received \"" << reinterpret_cast< const char* >(decrypted.data())
             << "\" " << request_nbr << endl;
    }
    zmq_close(requester);
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;
}
