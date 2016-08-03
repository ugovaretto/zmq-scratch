#include <cassert>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <zmq.h>
#include <sodium.h>
#include "sodium-util.h"

/////@todo add method versions for vector< T > input
//class CryptoCodec {
//public:
//    /// initialize with public key of other endpoint
//    CryptoCodec(const PublicKey& pk) : publicKey_(pk), keys_(GenKeys()), Nonce(GenNonce()) {}
//    size_t Encrypt(const void* in, size_t size, std::vector< unsigned char >& out) {
//        out.resize(cipherlen(size));
//        if(crypto_box_easy(out.data(),
//                           reinterpret_cast< const unsigned char* >(in),
//                           size, nonce_.data(),
//                           publicKey_.data(), keys_.secret.data()) != 0) {
//            throw std::runtime_error("Encryption failed");
//        }
//        return out.size();
//    }
//    size_t Decrypt(const void* in, size_t size, unsigned char* data) {
//        if(crypto_box_open_easy(data, reinterpret_cast< const unsigned char* >(in), size,
//                                nonce_.data(),
//                                publicKey_.data(),
//                                keys_.secret.data()) != 0) {
//            std::domain_error("Invalid data - cannot decrypt");
//        }
//        return bufferlen(size);
//    }
//    void SetSharedSecret(const void* encryptedSharedSecret) {
//        Decrypt(encryptedSharedSecret, sharedSecret_.size(), sharedSecret_.data());
//    }
//    size_t EncryptSS(const void* in, size_t size, std::vector< unsigned char >& out) {
//        out.resize(cipherlen(size));
//        if(crypto_box_easy_afternm(out.data(),
//                       reinterpret_cast< const unsigned char* >(in),
//                       size, nonce_.data(),
//                       sharedSecret_.data()) != 0) {
//            throw std::runtime_error("Error encrypting data");
//        }
//        return out.size();
//    }
//  size_t DecryptSS(const void* in, size_t size, std::vector< unsigned char >& out) {
//      out.resize(bufferlen(size));
//      if(crypto_box_open_easy_afternm(out.data(), reinterpret_cast< const unsigned char* >(int),
//                                      size,
//                                      nonce_.data(), sharedSecret_.data()) != 0) {
//          std::domain_error("Invalid data - cannot decrypt");
//      }
//  }
//private:
//  SharedSecret sharedSecret_;
//  KeyPair keys_;
//  PublicKey publicKey_;
//  Nonce nonce_;
//};
//
//

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

    //0 generate and send nonce
    Nonce nonce = GenNonce();
    zmq_send(requester, nonce.data(), nonce.size(), 0);
    zmq_recv(requester, nullptr, 0, 0);
    //1 generate keys
    KeyPair clientKeys = GenKeys();
    //2 send public key to other endpoint
    zmq_send(requester, clientKeys.pub.data(), clientKeys.pub.size(), 0);
    //3 receive public key from other endpoint
    PublicKey serverPublicKey;
    zmq_recv(requester, serverPublicKey.data(), serverPublicKey.size(), 0);
    //4 generate and send shared key through encrypted connection
    SharedSecret sharedSecret = GenShared(clientKeys);
    vector< unsigned char > ebuf(cipherlen(sharedSecret.size()));
    if(crypto_box_easy(ebuf.data(),
                       sharedSecret.data(),
                       sharedSecret.size(), nonce.data(),
                       serverPublicKey.data(), clientKeys.secret.data()) != 0) {
        perror("Encryption failed");
        return EXIT_FAILURE;
    }

    zmq_send(requester, ebuf.data(), ebuf.size(), 0);
    zmq_recv(requester, nullptr, 0, 0);
    Zero(clientKeys);
    //5 encrypt-send / decrypt-receive loop
    int request_nbr;
    for (request_nbr = 0; request_nbr != 10; request_nbr++) {
        vector< unsigned char > buffer(cipherlen(0x1000));
        if(crypto_box_easy_afternm(buffer.data(),
                           reinterpret_cast< const unsigned char* >("Hello"),
                           5, nonce.data(),
                           sharedSecret.data()) != 0) {
            perror("Encryption failed");
            return EXIT_FAILURE;
        }
        zmq_send(requester, buffer.data(), cipherlen(5), 0);
        const size_t sz = zmq_recv(requester, buffer.data(), buffer.size(), 0);
        vector< unsigned char > decrypted(bufferlen(sz));
        if(crypto_box_open_easy_afternm(decrypted.data(), buffer.data(), sz,
                                        nonce.data(), sharedSecret.data()) != 0) {
            perror("Shared secret decryption failed");
            return EXIT_FAILURE;
        }
        cout << "Received \"" << reinterpret_cast< const char* >(decrypted.data())
             << "\" " << request_nbr << endl;
    }
    zmq_close(requester);
    zmq_ctx_destroy(context);
    return EXIT_SUCCESS;
}
