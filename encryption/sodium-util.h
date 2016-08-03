//
// Created by Ugo Varetto on 8/3/16.
//

#ifndef ZMQ_SCRATCH_SODIUM_UTIL_H
#define ZMQ_SCRATCH_SODIUM_UTIL_H
#include <array>
#include <sodium.h>

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

#endif //ZMQ_SCRATCH_SODIUM_UTIL_H
