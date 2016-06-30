#pragma once
//
// Created by Ugo Varetto on 6/29/16.
//
#include <stdexcept>
#include <thread>
#include <tuple>
#include <string>

#include <zmq.h>

#include "SyncQueue.h"


//usage
// const char* subscribeURL = "tcp://localhost:5556";
// RAWInstream< int > is;
// auto handleData = [](int i) { cout << i << endl; };
// int inactivityTimeoutInSec = 10; //optional, not currently supported
// //blocking call, will stop at the reception of an empty message
// is.Start(subscribeURL, handleData, inactivityTimeoutInSec);


template < typename DataT >
struct DefaultDeSerializer {
    DataT operator()(const char* buf, int size) const {
        assert(buf);
        assert(size > 0);
        ///@todo add Size and Copy(void*, data) customizations
        DataT d;
        if(sizeof(d) != size)
            throw std::domain_error("Wrong data size");
        memmove(&d, ptr, size);
        return d;
    }
};

template < typename T >
struct DefaultDeSerializer< std::vector< T > > {
    std::vector< T > operator()(const char* buf, int size) const {
        assert(buf);
        assert(size > 0);
        const T* begin = reinterpret_cast< const T *>(buf);
        const size_t size = size / sizeof(T);
        return std::vector< T >(begin, begin + size);
    }
};

template< typename DataT,
          typename DeSerializerT = DefaultDeSerializer< DataT > >
class RAWInStream {
    using Callback = std::function(const DataT&);
public:
    RAWInStream() = delete;
    RAWInStream(const RAWInStream&) = delete;
    RAWInStream(RAWInStream&&) = default;
    RAWInStream(const char *URI, const DeSerializerT& d = DeSerializerT())
            : deserialize_(d), stop_(false) {
        Start();
    }
    bool Stop() { //call from separate thread
        stop_ = true;
    }
    void Loop(const Callback& cback) { //sync
        while(true) {
            if(!cback(queue_.Pop())) break;
        }
    }
    ~RAWInStream() {
        Stop();
    }
private:
    void Start(const char* URI, const Callback& cback,
               int bufsize, int inactivityTimeout) { //async
        taskFuture_
                = std::async(std::launch::async,
                             CreateWorker(), URI, cback,
                             bufsize, inactivityTimeout);
    }
    std::function< void (const char*, size_t, int) >
    CreateWorker() const {
        return [this](const char* URI,
                      size_t bufsize,
                      int inactivityTimeoutInSec) {
            this->Execute(URI, bufsize, inactivityTimeoutInSec);
        };
    }
    void Execute(const char* URI,
                 size_t bufferSize = 0x100000,
                 int timeoutInSeconds = -1 /*not implemented*/ ) { //sync
        void* ctx = nullptr;
        void* sub = nullptr;
        std::tie(ctx, sub) = CreateZMQContextAndSocket();
        std::vector< char > buffer(bufferSize);
        while(stop_) {
            int rc = zmq_recv(sub, buffer.data(), buffer.size(), 0);
            if(rc < 0) throw std::runtime_error("Error receiving data");
            if(rc == 0 || stop_) break;
            queue_.push(deserialize_(buffer.data(), rc));
        }
    }
private:
    void CleanupZMQResources(void* ctx, void* sub) {
        if(sub) zmq_close(sub);
        if(ctx) zmq_ctx_destroy(ctx);
    }
    std::tuple< void*, void* > CreateZMQContextAndSocket(const char* URI) {
        void *ctx = nullptr;
        void *sub = nullptr;
        try {
            ctx = zmq_ctx_new();
            if(!ctx) throw std::runtime_error("Cannot create ZMQ context");
            sub = zmq_socket(ctx, ZMQ_SUB);
            if(!sub) throw std::runtime_error("Cannot create ZMQ SUB socket");
            if(!zmq_connect(sub, URI))
                throw std::runtime_error("Cannot connect to "
                                         + std::string(URI));
            if(!zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0))
                throw std::runtime_error("Cannot set ZMQ_SUBSCRIBE flag");
            return std::make_tuple(ctx, sub);
        } catch(const std::exception& e) {
            CleanupZQMResources(ctx, sub);
            throw e;
        }
        return std::make_tuple(nullptr, nullptr);
    };
private:
    SyncQueue< DataT > queue_;
    std::future< void > taskFuture_;
    DeSerializerT deserialize_ = DeSerializerT();
    bool stop_ = false;
};
