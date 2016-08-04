#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>

#include <zmq.h>
#include <sodium.h>
#include "sodium-util.h"


using namespace std;

int main(int, char**) {
    //  Socket to talk to clients
    void* context = ZCheck(zmq_ctx_new());
    void* responder = ZCheck(zmq_socket(context, ZMQ_REP));
    int rc = ZCheck(zmq_bind(responder, "tcp://*:5555"));

    const bool INITIATE_OPTION = false;
    const SecretNonce sn = HandShake(responder, INITIATE_OPTION);
    const SharedSecret& sharedSecret = sn.sharedSecret;
    const Nonce nonce = sn.nonce;

    //receive-decrypt / encrypt-send loop
    vector< unsigned char > tmpbuffer(cipherlen(0x100));
    vector< unsigned char > decrypted;
    while(true) {
        const size_t receivedData =
            size_t(ZCheck(zmq_recv(responder, tmpbuffer.data(), tmpbuffer.size(), 0)));
        Decrypt(tmpbuffer.data(), receivedData, sharedSecret, nonce, decrypted);
        cout << "Received \"" << reinterpret_cast< const char* >(decrypted.data())
             <<  '\"' << endl;
	    this_thread::sleep_for(chrono::seconds(1));          //  Do some 'work'
        const char* msg = "World";
        const vector< unsigned char >& buffer = Encrypt(msg, strlen(msg),
                                                        sharedSecret, nonce, tmpbuffer);
        ZCheck(zmq_send(responder, buffer.data(), buffer.size(), 0));
    }
    ZCheck(zmq_close(responder));
    ZCheck(zmq_ctx_destroy(context));
    return EXIT_SUCCESS;
}
