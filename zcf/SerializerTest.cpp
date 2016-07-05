//
// Created by Ugo Varetto on 7/4/16.
//

#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>
#include <tuple>

//#define LOG__ 1

#if LOG__
#include <algorithm>
#include <iterator>
#endif

#include "Serialize.h"

using namespace std;

int main(int, char**) {
    //POD
    const int intOut = 3;
    using IntSerializer = GetSerializer< decltype(intOut) >::Type;
    static_assert(std::is_same< IntSerializer, SerializePOD< int > >::value,
                  "Not SerializePOD< int > type");
    const ByteArray ioutBuf = IntSerializer::Pack(intOut);
    int intIn = -1;
    IntSerializer::UnPack(begin(ioutBuf), intIn);
    assert(intOut == intIn);
    //POD vector
    const vector< int > vintOut = {1,2,3,4,5,4,3,2,1};
    using VIntSerializer = GetSerializer< decltype(vintOut) >::Type;
    static_assert(std::is_same< VIntSerializer, SerializeVectorPOD< int > >::value,
                  "Not SerializeVectorPOD< int > type");
    const ByteArray voutBuf = VIntSerializer::Pack(vintOut);
    vector< int > vintIn;
    VIntSerializer::UnPack(begin(voutBuf), vintIn);
#if LOG__
    cout << endl;
    copy(vintIn.begin(), vintIn.end(), ostream_iterator< int >(cout, " "));
    cout << endl;
#endif
    assert(vintIn.size() == vintOut.size());
    assert(vintIn == vintOut);
    //string
    const string outstring = "outstring";
    using StringSerializer = GetSerializer< decltype(outstring) >::Type;
    static_assert(std::is_same< StringSerializer, SerializeString >::value,
                  "Not SerializeString type");
    const ByteArray soutBuf = StringSerializer::Pack(outstring);
    string instring;
    StringSerializer::UnPack(begin(soutBuf), instring);
    assert(instring.size() == outstring.size());
    assert(instring == outstring);
    //Non-POD tuple
    const tuple< int, double, string > outTuple = make_tuple(4, 4.0, "four");
    using TupleSerializer = GetSerializer< decltype(outTuple) >::Type;
    static_assert(std::is_same< TupleSerializer,
                  Serialize< std::remove_cv< decltype(outTuple) >
                  ::type > >::value,
                  "Not Serialize< tuple< int, double, string > > type");
    const ByteArray toutBuf = TupleSerializer::Pack(outTuple);
    tuple< int, double, string > inTuple;
    TupleSerializer::UnPack(begin(toutBuf), inTuple);
    assert(inTuple == outTuple);
    //POD tuple
    const tuple< int, double, float > poutTuple = make_tuple(4, 4.0, 4.0f);
    using PODTupleSerializer = GetSerializer< decltype(poutTuple) >::Type;
    static_assert(std::is_same< PODTupleSerializer,
                          SerializePOD< std::remove_cv< decltype(poutTuple) >
                          ::type > >::value,
                  "Not SerializePOD< tuple< int, double, float > > type");
    const ByteArray ptoutBuf = PODTupleSerializer::Pack(poutTuple);
    tuple< int, double, float > pinTuple;
    PODTupleSerializer::UnPack(begin(ptoutBuf), pinTuple);
    assert(pinTuple == poutTuple);
    //Non-POD vector
    const vector< string > vsout = {"1", "2", "three"};
    using VSSerializer = GetSerializer< vector< string > >::Type;
    static_assert(std::is_same< VSSerializer,
                                SerializeVector< string > >::value,
                  "Not SerializeVector type");
    const ByteArray vsoutBuf = VSSerializer::Pack(vsout);
    vector< string > vsin;
    VSSerializer::UnPack(begin(vsoutBuf), vsin);
#if LOG__
    cout << endl;
    copy(vsin.begin(), vsin.end(), ostream_iterator< string >(cout, "\n"));
    cout << endl;
#endif
    assert(vsin == vsout);
    return EXIT_SUCCESS;
}