//
// Created by Ugo Varetto on 8/3/16.
//

#include <cstdlib>
#include <array>
#include <iostream>

#include <sodium.h>

using PublicKey = std::array< unsigned char, crypto_box_PUBLICKEYBYTES >;
using SecretKey = std::array< unsigned char, crypto_box_SECRETKEYBYTES >;
using Noonce    = std::array< unsigned char, crypto_box_NONCEBYTES >;
using SharedSecret = std::array< unsigned char, crypto_box_BEFORENMBYTES >;


constexpr size_t cipherlen(size_t messageLength) {
    return messageLength + crypto_box_MACBYTES;
}


struct KeyPair {
    PublicKey pub;
    ///@todo use sodium_mallock for secret
    SecretKey secret;
};

KeyPair GenKeys() {
    KeyPair k;
    crypto_box_keypair(k.pub.data(), k.secret.data());
    return k;
}

SharedSecret GenShared(const KeyPair& kp) {
    SharedSecret s;
    if(crypto_box_beforenm(s.data(), kp.pub.data(), kp.secret.data()) != 0) {}
    return s;
}

Noonce GenNonce() {
    std::array< unsigned char, crypto_box_NONCEBYTES > nonce;
    randombytes_buf(nonce.data(), nonce.size());
    return nonce;
}

void Zero(KeyPair& kp) {
    sodium_memzero(kp.pub.data(), kp.pub.size());
    sodium_memzero(kp.secret.data(), kp.secret.size());
}

int main(int, char**) {

    const char* MESSAGE = "test";
    const size_t MESSAGE_LEN = 4;

    KeyPair alice = GenKeys();
    KeyPair bob   = GenKeys();
    Noonce nonce = GenNonce();
    std::array< unsigned char, cipherlen(MESSAGE_LEN) > text;

    if(crypto_box_easy(text.data(),
                        reinterpret_cast< const unsigned char* >(MESSAGE),
                        MESSAGE_LEN, nonce.data(),
                        bob.pub.data(), alice.secret.data()) != 0) {
        perror("Encryption failed");
        return EXIT_FAILURE;
    }

    unsigned char decrypted[MESSAGE_LEN];
    if(crypto_box_open_easy(decrypted, text.data(), text.size(),
                             nonce.data(),
                             alice.pub.data(),
                             bob.secret.data()) != 0) {
        perror("Decryption failed");
        return EXIT_FAILURE;
    }
    std::cout << "public-private OK" << std::endl;


    SharedSecret ss = GenShared(bob);

    Zero(bob);
    Zero(alice);
    if(crypto_box_easy_afternm(text.data(),
                               reinterpret_cast< const unsigned char* >(MESSAGE),
                               MESSAGE_LEN, nonce.data(), ss.data()) != 0) {
        perror("Shared secret encryption failed");
    }

    if(crypto_box_open_easy_afternm(decrypted, text.data(), text.size(),
                                    nonce.data(), ss.data()) != 0) {
        perror("Shared secret decription failed");
    }
    std::cout << "shared key OK" << std::endl;
    return EXIT_SUCCESS;
}
