//
// Created by Ugo Varetto on 7/1/16.
//
#include <thread>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <iostream>

#include "RAWInStream.h"
#include "RAWOutStream.h"

using namespace std;

int main(int, char**) {
    //ipc and tcp do work inproc does not!
    const char* URI = "ipc://outstream";

    const int NUM_MESSAGES = 10;
    const int MESSAGE_SIZE = 0x100000;

    //SUB
    int received = 0;
    auto receiver = [&received, NUM_MESSAGES, MESSAGE_SIZE](const char* uri) {
        RAWInStream< vector< char > > is(uri, MESSAGE_SIZE);
        is.Loop([&received, NUM_MESSAGES](const vector< char >& v) {
            if(!v.empty()) cout << ++received << endl;
            return !v.empty(); });
    };
    auto f = async(launch::async, receiver, URI);

    //PUB
    RAWOutStream< vector< char > > os(URI);
    std::vector< char > data(MESSAGE_SIZE);
    for(int i = 0; i != NUM_MESSAGES; ++i) {
        os.Send(data);
        using namespace chrono;
        this_thread::sleep_for(duration_cast< nanoseconds >(milliseconds(200)));
    }
    os.Stop();
    f.wait();
    return EXIT_SUCCESS;
}
