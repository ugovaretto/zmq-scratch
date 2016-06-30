#pragma once
//
// Created by Ugo Varetto on 6/29/16.
//
#include <stdexcept>
#include <thread>
#include <tuple>
#include <future>
#include <chrono>

#include <zmq.h>

#include "SyncQueue.h"


//RAWOutStream< int > os;
//os.Send(3);

template < typename DataT >
struct DefaultSerializer {
    std::vector< char > operator()(const DataT& d) const {
        ///@todo add Size and Copy(data, void*) customizations
        std::vector< char > v(sizeof(d));
        const char* p = reinterpret_cast< const char* >(&d);
        std::copy(p, p + sizeof(p), v.begin());
        return v;
    }
};

template < typename T >
struct DefaultSerializer< std::vector< T > > {
    std::vector< char > operator()(const std::vector< T >& v) const {
        const size_t bytesize
                = v.size() * sizeof(std::vector< T >::value_type);
        std::vector< char > r(v.size() * sizeof(std::vector< T >::value_type));
        const char* begin = reinterpret_cast< const char* >(v.data());
        memmove(r.data(), begin, begin + bytesize);
        return r;
    }
};

template< typename DataT, typename SerializerT = DefaultSerializer< DataT > >
class RAWOutStream {
public:
    RAWOutStream() = delete;
    RAWOutStream(const RAWOutStream&) = delete;
    RAWOutStream(RAWOutStream&&) = default;
    RAWOutStream(const char *URI, const SerializerT& S = SerializerT())
            : serialize_(S) {
        Start(URI);
    }
    void Send(const DataT &data) { //async
        queue_.Push(data);
    }
    template < typename FwdT >
    void Buffer(FwdT begin, FwdT end) {
        queue_.Buffer(begin, end);
    }
    ///stops if the result of serialize_(DataT()) is an empty vector<char>
    ///@param timeoutSeconds file stop request then wait until timeout before
    ///       returning
    bool Stop(int timeoutSeconds = 4) { //sync
        queue_.PushFront(DataT());
        const std::future_status =
                taskFuture_.wait_for(std::chrono::seconds(timeoutSeconds));
        return std::future_status == std::future_status_ready;
    }
    ~RAWOutStream() {
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
    void CleanupZMQResources(void* ctx, void* pub) {
        if(pub) zmq_close(pub);
        if(ctx) zmq_ctx_destroy(ctx);
    }
    std::tuple< void*, void* > CreateZMQContextAndSocket(const char* URI) {
        void *ctx = nullptr;
        void *pub = nullptr;
        try {
            ctx = zmq_ctx_new();
            if(!ctx) throw std::runtime_error("Cannot create ZMQ context");
            pub = zmq_socket(ctx, ZMQ_PUB);
            if(!pub) throw std::runtime_error("Cannot create ZMQ PUB socket");
            if(zmq_bind(pub, URI))
                throw std::runtime_error("Cannot bind ZMQ socket");
            return std::make_tuple(ctx, pub);
        } catch(const std::exception& e) {
            CleanupZQMResources(ctx, pub);
            throw e;
        }
        return std::make_tuple(nullptr, nullptr);
    };
private:
    SyncQueue< DataT > queue_;
    std::future< void > taskFuture_;
    SerializerT serialize_ = SerializerT();
};
