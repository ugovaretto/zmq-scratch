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

int main(int argc, char** argv) {
    if(argc != 4) {
        cerr << "usage: "
             << argv[0] << "<-pub | -sub> <URI> <num messages>" << endl;
        return EXIT_FAILURE;
    }
    const bool server = argv[1] == string("-pub");
    const char* URI = argv[2];
    const int numMessages = stoi(argv[3]);
    assert(numMessages > 0);
    if(server) {
        RAWOutStream< vector< char > > os(URI);
        std::vector< char > data(0x100000);
        for(int i = 0; i != numMessages; ++i) {
            os.Send(data);
            using namespace chrono;
            this_thread::sleep_for(
                    duration_cast< nanoseconds >(seconds(1)));
        }
    } else {
        int received = 0;
        RAWInStream< vector< char > > is(URI);
        is.Loop([&received, numMessages](const vector<char> &) {
            cout << ++received << endl;
            return received < numMessages - 1;
        });
    }
    return EXIT_SUCCESS;
}
