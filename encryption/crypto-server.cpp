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
    void* context = zmq_ctx_new();
    void* responder = zmq_socket(context, ZMQ_REP);
    int rc = zmq_bind(responder, "tcp://*:5555");
    assert(rc == 0);

    //0 receive noonce
    Nonce nonce;
    zmq_recv(responder, nonce.data(), nonce.size(), 0);
    zmq_send(responder, nullptr, 0, 0);
    //1 generate keys
    KeyPair serverKeys = GenKeys();
    //2 receive public key from client
    PublicKey clientPublicKey;
    zmq_recv(responder, clientPublicKey.data(), clientPublicKey.size(), 0);
    //3 send public key to client
    zmq_send(responder, serverKeys.pub.data(), serverKeys.pub.size(), 0);
    //4 receive shared secret from client
    SharedSecret sharedSecret;
    vector< unsigned char > ebuf(cipherlen(sharedSecret.size()));
    zmq_recv(responder, ebuf.data(), ebuf.size(), 0);

    if(crypto_box_open_easy(sharedSecret.data(), ebuf.data(), ebuf.size(),
                            nonce.data(),
                            clientPublicKey.data(),
                            serverKeys.secret.data()) != 0) {
        perror("Decryption failed");
        return EXIT_FAILURE;
    }

    //5 send ACK
    zmq_send(responder, nullptr, 0, 0);

    //6 receive-decrypt / encrypt-send loop
    while(true) {
        vector< unsigned char > buffer(cipherlen(0x1000));
        const size_t sz = zmq_recv(responder, buffer.data(), buffer.size(), 0);
        vector< unsigned char > decrypted(bufferlen(sz), '\0');
        if(crypto_box_open_easy_afternm(decrypted.data(), buffer.data(), sz,
                                        nonce.data(), sharedSecret.data()) != 0) {
            perror("Shared secret decryption failed");
            for(auto c: buffer) cout << hex << int(c) << ' ';
            cerr << endl;
            for(auto c: decrypted) cout << hex << int(c) << ' ';
            cerr << endl;
            return EXIT_FAILURE;
        }
        cout << "Received \"" << reinterpret_cast< const char* >(decrypted.data())
             <<  '\"' << endl;
	    this_thread::sleep_for(chrono::seconds(2));          //  Do some 'work'
        if(crypto_box_easy_afternm(buffer.data(),
                                   reinterpret_cast< const unsigned char* >("World"),
                                   5, nonce.data(),
                                   sharedSecret.data()) != 0) {
            perror("Encryption failed");
            return EXIT_FAILURE;
        }
        zmq_send(responder, buffer.data(), cipherlen(5), 0);
    }
    return EXIT_SUCCESS;
}
