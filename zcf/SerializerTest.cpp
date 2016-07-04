//
// Created by Ugo Varetto on 7/4/16.
//

#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

#include "Serialize.h"

using namespace std;

int main(int, char**) {


    const int intOut = 3;
    using IntSerializer = GetSerializer< decltype(intOut) >::Type;
    const ByteArray ioutBuf = IntSerializer::Pack(intOut);
    int intIn = -1;
    IntSerializer::UnPack(ioutBuf, intIn);
    assert(intOut == intIn);

    const vector< int > vintOut = {1,2,3,4,5,4,3,2,1};
    using VIntSerializer = GetSerializer< decltype(vintOut) >::Type;
    const ByteArray voutBuf = VIntSerializer::Pack(vinInt);
    vector< int > vintIn;
    VIntSerializer::UnPack(voutBuf, vintIn);
    assert(vintIn, vintOut);

    return EXIT_SUCCESS;
}