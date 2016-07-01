#pragma once
//
// Created by Ugo Varetto on 7/1/16.
//
#include "RAWInStream.h;
#include "RAWOutStream.h;
//usage
// RAWInOutStream< int, string > ios("ipc://intsource",
//                                   inbuffersize,
//                                   intimeout,
//                                   DefaultSerializer< int >(),
//                                   "ipc://stringsink",
//                                   StringSerializer());
// auto filter = [](int in, string& out) {
//                  out = to_string(in);
//                  return in == in; //NaN -> false
//                };
// ios.Forward(filter); //sync


template< typename InT, typename OutT >
class RAWInOutStream {
    using Filter = std::function< bool (const InT&, OutT&) >;
public:
    template < typename InDeSerializerT, typename OutSerializerT >
    RAWInOutStream(const char* subURI, int inbufsize, int intimeout,
                   const InDeSerializerT& deserializer,
                   const char* pubURI, const OutSerializerT& serializer) :
        is_(subURI, inbufsize, intimeout, deserializer),
        os_(pubURI, serializer) {}
    RAWInOutStream(const RAWInOutStream&) = delete;
    RAWInOutStream() = delete;
    RAWInOutStream(RAWInOutStream&&) = default;
    RAWInOutStream& operator=(const RAWInOutStream&) = delete;
    void Filter(const Filter& F) {
        auto f = [F, this](const InT& in) {
            OutT out;
            const bool r = F(in, out);
            if(r) this->out_.Send(out);
            else this->out_.Stop();
            return r;
        };
        is_.Loop(f);
    }
    void Stop() {
        is_.Stop();
        os_.Stop();
    }
private:
    RAWInStream is_;
    RAWOutStream out_;
};