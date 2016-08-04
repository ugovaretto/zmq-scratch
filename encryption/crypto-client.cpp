#include <cassert>
#include <iostream>
#include <cstdlib>
#include <vector>

#include <zmq.h>
#include <sodium.h>
#include "sodium-util.h"
#include "../utility.h"


using namespace std;

int main(int argc, char **argv) {
    if (argc < 3) {
        cerr << "usage: " << argv[0] << " <server ip address> <port>" << endl;
        return EXIT_FAILURE;
    }
    const string remoteAddress = "tcp://" + string(argv[1]) + ":" + string(argv[2]);
    cout << "Connecting to hello world server " << remoteAddress << "…" << endl;
    void *context = ZCheck(zmq_ctx_new());
    void *requester = ZCheck(zmq_socket(context, ZMQ_REQ));
    ZCheck(zmq_connect(requester, remoteAddress.c_str()));

    const bool INITIATE_OPTION = true;
    const SecretNonce sn = HandShake(requester, INITIATE_OPTION);
    const SharedSecret& sharedSecret = sn.sharedSecret;
    const Nonce nonce = sn.nonce;
    //encrypt-send / decrypt-receive loop
    int request = 1;
    vector< unsigned char > tmpbuffer(cipherlen(0x100));
    vector< unsigned char > decrypted;
    while(true) {
        const char* msg = "Hello";
        const vector< unsigned char >& buffer = Encrypt(msg, strlen(msg),
                                                        sharedSecret, nonce, tmpbuffer);
        ZCheck(zmq_send(requester, buffer.data(), buffer.size(), 0));
        const size_t sz = ZCheck(size_t(zmq_recv(requester, tmpbuffer.data(), tmpbuffer.size(), 0)));
        Decrypt(tmpbuffer.data(), tmpbuffer.size(), sharedSecret, nonce, decrypted);
        cout << "Received \"" << reinterpret_cast< const char* >(decrypted.data())
             << "\" " << request << endl;
        ++request;
    }
    ZCheck(zmq_close(requester));
    ZCheck(zmq_ctx_destroy(context));
    return EXIT_SUCCESS;
}
