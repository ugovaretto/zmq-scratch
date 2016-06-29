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


template < typename DataT >
struct DefaultDeSerializer {
    DataT operator()(const std::vector< char >& buffer) const {
        ///@todo add Size and Copy(void*, data) customizations
        DataT d;
        if(sizeof(d) != buffer.size())
            throw std::domain_error("Wrong data size");
        memmove(&d, buffer.data(), buffer.size());
        return d;
    }
};

template< typename DataT,
          typename DeSerializerT = DefaultDeSerializer< DataT > >
class RAWInStream {
public:
    RAWInStream() = delete;
    RAWInStream(const RAWInStream&) = delete;
    RAWInStream(RAWInStream&&) = default;
    RAWInStream(const char *URI, const DeSerializerT& D = DeSerializerT())
            : deserialize_(D) {
    }
    void Start() { //async
        
    }
    template < typename FwdT >
    void Buffer(FwdT begin, FwdT end) {
        queue_.Buffer(begin, end);
    }
    void Stop() { //sync
        queue_.PushFront(DataT());
        taskFuture_.wait();
    }
    ~RAWInStream() {
        Stop();
    }
private:
    void Start(const char* URI) {
        taskFuture_
                = std::async(std::launch::async, CreateWorker(), URI);
    }
    std::function< void (const char*) > CreateWorker() const {
        return [this](const char* URI) {
            this->Execute(URI);
        };
    }
    void Execute(const char* URI) {
        void* ctx = nullptr;
        void* pub = nullptr;
        std::tie(ctx, pub) = CreateZMQContextAndSocket(URI)
        ///@todo add timeout support
        while(true) {
            const DataT d(std::move(queue_.Pop()));
            std::vector<char> buffer(serialize_(d));
            zmq_send(pub, buffer.data(), buffer.size(), 0);
            if(buffer.empty()) break;
        }
        CleanupZMQResources(ctx, pub);
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
    SerializerT serialize_;
};
