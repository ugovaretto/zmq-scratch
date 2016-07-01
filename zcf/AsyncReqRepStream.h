#pragma once
//
// Created by Ugo Varetto on 7/1/16.
//
#include "RAWInStream.h;
#include "RAWOutStream.h;
//usage
// RAWAsyncReqStream requestor(requestPubSource, replySubSource);
// std::vector< char > request = MakeRequest(...);
// auto replyCallback = [](int i) { cout << i << endl; }
// rr.SendRequest(request, expectReply, replyCallback);

// RAWAsyncRepStream replier(replyPubSource  requestSubSource;
// auto requestHandler = [](int r){ return 2*i; };
// rr.StartServicingRequests(requestHandler);



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