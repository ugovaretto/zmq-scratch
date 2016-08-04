//
// Created by Ugo Varetto on 8/3/16.
//

#pragma once
#include <array>
#include <string>
#include <cstring>
#include <stdexcept>
#include <sodium.h>
#include "../utility.h"

using PublicKey = std::array< unsigned char, crypto_box_PUBLICKEYBYTES >;
using SecretKey = std::array< unsigned char, crypto_box_SECRETKEYBYTES >;
using Nonce    = std::array< unsigned char, crypto_box_NONCEBYTES >;
using SharedSecret = std::array< unsigned char, crypto_box_BEFORENMBYTES >;

constexpr size_t cipherlen(size_t messageLength) {
    return messageLength + crypto_box_MACBYTES;
}

constexpr size_t bufferlen(size_t cipherLength) {
    return cipherLength - crypto_box_MACBYTES;
}

struct KeyPair {
    PublicKey pub;
    ///@todo use sodium_mallock and shared_ptr for secret
    ///then add Pub and Secret methods to access data pointer
    ///and PubSize SecretSize to access buffer sizes
    SecretKey secret;
};

inline KeyPair GenKeys() {
    KeyPair k;
    crypto_box_keypair(k.pub.data(), k.secret.data());
    return k;
}

void Init(void* p, size_t s) {
    sodium_memzero(p, s);
}

bool IsZero(const void* p, size_t s) {
    return bool(sodium_is_zero(reinterpret_cast< const unsigned char* >(p), s));
}

inline SharedSecret GenShared(const KeyPair& kp) {
    SharedSecret s;
    if(crypto_box_beforenm(s.data(), kp.pub.data(), kp.secret.data()) != 0) {}
    return s;
}

inline Nonce GenNonce() {
    std::array< unsigned char, crypto_box_NONCEBYTES > nonce;
    randombytes_buf(nonce.data(), nonce.size());
    return nonce;
}

inline void Zero(KeyPair& kp) {
    sodium_memzero(kp.pub.data(), kp.pub.size());
    sodium_memzero(kp.secret.data(), kp.secret.size());
}


struct SecretNonce {
  SharedSecret sharedSecret;
  Nonce nonce;
};

//namespace {
SecretNonce HandShake(void *s, bool initiate) {
    if(initiate) {
        //0 generate and send nonce
        Nonce nonce = GenNonce();
        ZCheck(zmq_send(s, nonce.data(), nonce.size(), 0));
        ZCheck(zmq_recv(s, nullptr, 0, 0));
        //1 generate keys
        KeyPair clientKeys = GenKeys();
        //2 send public key to other endpoint
        ZCheck(zmq_send(s, clientKeys.pub.data(), clientKeys.pub.size(), 0));
        //3 receive public key from other endpoint
        PublicKey serverPublicKey;
        ZCheck(zmq_recv(s, serverPublicKey.data(), serverPublicKey.size(), 0));
        //4 generate and send shared key through encrypted connection
        SharedSecret sharedSecret = GenShared(clientKeys);
        std::vector< unsigned char > ebuf(cipherlen(sharedSecret.size()));
        if (crypto_box_easy(ebuf.data(),
                            sharedSecret.data(),
                            sharedSecret.size(), nonce.data(),
                            serverPublicKey.data(), clientKeys.secret.data()) != 0) {
            throw std::runtime_error("Encryption failed: " + std::string(strerror(errno)));
        }

        ZCheck(zmq_send(s, ebuf.data(), ebuf.size(), 0));
        ZCheck(zmq_recv(s, nullptr, 0, 0));
        Zero(clientKeys);
        return {sharedSecret, nonce};
    } else {
        //0 receive noonce
        Nonce nonce;
        ZCheck(zmq_recv(s, nonce.data(), nonce.size(), 0));
        ZCheck(zmq_send(s, nullptr, 0, 0));
        //1 generate keys
        KeyPair serverKeys = GenKeys();
        //2 receive public key from client
        PublicKey clientPublicKey;
        ZCheck(zmq_recv(s, clientPublicKey.data(), clientPublicKey.size(), 0));
        //3 send public key to client
        ZCheck(zmq_send(s, serverKeys.pub.data(), serverKeys.pub.size(), 0));
        //4 receive shared secret from client
        SharedSecret sharedSecret;
        std::vector< unsigned char > ebuf(cipherlen(sharedSecret.size()));
        ZCheck(zmq_recv(s, ebuf.data(), ebuf.size(), 0));

        if (crypto_box_open_easy(sharedSecret.data(), ebuf.data(), ebuf.size(),
                                 nonce.data(),
                                 clientPublicKey.data(),
                                 serverKeys.secret.data()) != 0) {
            throw std::domain_error("Decryption failed: " + std::string(strerror(errno)));
        }

        //5 send ACK
        ZCheck(zmq_send(s, nullptr, 0, 0));
        Zero(serverKeys);
        return {sharedSecret, nonce};
    }
}

const std::vector< unsigned char >& Encrypt(const void *data,
                                            size_t size,
                                            const SharedSecret &sharedSecret,
                                            const Nonce &nonce,
                                            std::vector<unsigned char> &out) {
    out.resize(cipherlen(size));
    if (crypto_box_easy_afternm(out.data(),
                                reinterpret_cast< const unsigned char * >(data),
                                size, nonce.data(),
                                sharedSecret.data()) != 0) {
        throw std::runtime_error("Encryption failed - " + std::string(strerror(errno)));
    }
    return out;
};

const std::vector< unsigned char >& Decrypt(const void *in,
                                            size_t size,
                                            const SharedSecret &sharedSecret,
                                            const Nonce &nonce,
                                            std::vector<unsigned char> &out) {
    out.resize(bufferlen(size));
    if (crypto_box_open_easy_afternm(out.data(),
                                     reinterpret_cast< const unsigned char * >(in),
                                     size,
                                     nonce.data(),
                                     sharedSecret.data()) != 0) {
        throw std::runtime_error("Encryption failed - " + std::string(strerror(errno)));
    }
    return out;
}
//}

