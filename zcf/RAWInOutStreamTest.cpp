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
    const char* URI = "inproc://out-stream";
    int received = 0;
    auto receiver = [&received](const char* uri) {
        RAWInStream< vector< char > > is(uri);
        is.Loop([&received](const vector< char >& ) {
            cout << ++received << endl; return true; });
    };
    auto f = async(launch::async, receiver, URI);

    RAWOutStream< vector< char > > os(URI);
    std::vector< char > data(0x100000);
    for(int i = 0; i != 100; ++i) {
        os.Send(data);
        using namespace chrono;
        this_thread::sleep_for(duration_cast< nanoseconds >(milliseconds(200)));
    }
    os.Stop();
    f.wait();
    assert(received == 100);
    return EXIT_SUCCESS;
}
