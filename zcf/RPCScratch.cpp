//
// Created by Ugo Varetto on 7/4/16.
//

///@todo Make type safe: no check is performed when de-serializing
///consider optional addition of type information for each serialized type
///return method signature and description from service
///add ability to interact with service manager asking for supported services
///consider having service manager select URI based on workload, can be local
///or remote, in this case a protocol must be implemented to allow service
///managers to talk to each other and exchange information about supported
///services and current workload

///@todo need a way to derermine if service is under stress e.g. interval
///between end of request and start of new one

///@todo parameterize buffer size, timeout and option to send buffer size along
///data to allow for dynamic buffer resize

#include <memory>
#include <string>
#include <iterator>
#include <vector>
#include <map>
#include <future>
#include <thread>
#include <cstdlib>
#include <zmq.h>
#include <iostream>
#include <cassert>

#include "Serialize.h"
#include "SyncQueue.h"

///@todo use fixed number of workers or thread pool

void ZCheck(int v) {
    if(v < 0) {
        throw std::runtime_error(std::string("ZEROMQ ERROR: ")
                                 + strerror(errno));
    }
}

void ZCleanup(void *context, void *zmqsocket) {
    ZCheck(zmq_close(zmqsocket));
    ZCheck(zmq_ctx_destroy(context));
}

//IService

class ServiceImpl {
public:
    void Add(int id, const MethodImpl& mi) {
        methods_[id] = mi;
    }
    template < typename R, typename...ArgsT >
    void Add(int id, std::function< R (ArgsT...) > f) {
        Add(id, MakeMethod(f));
    };
    ByteArray Invoke(int reqid, const ByteArray& args) {
        return methods_[reqid].Invoke(args);
    }
private:
    std::map< int, MethodImpl > methods_;
};

//Service

class Service {
public:
    enum Status {STOPPED, STARTED};
public:
    Service(const std::string& URI, const ServiceImpl& si)
            : status_(STOPPED), uri_(URI), service_(si) {}
    Status GetStatus() const  { return status_; }
    std::string GetURI() const {
        return uri_;
    }
    string Start() {
        status_ = STARTED;
        void* ctx = zmq_ctx_new();
        void* r = zmq_socket(ctx, ZMQ_ROUTER);
        zmq_bind(r, uri_.c_str());
        zmq_pollitem_t items[] = { { r, 0, ZMQ_POLLIN, 0 } };
        int reqid = -1;
        ByteArray args(0x10000);
        ByteArray rep;
        vector< char > id(10, char(0));
        while(status_ != STOPPED) {
            zmq_poll(items, 1, 20); //poll with 20ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                const int irc = zmq_recv(r, &id[0], id.size(), 0);
                ZCheck(irc);
                ZCheck(zmq_recv(r, 0, 0, 0));
                int rc = zmq_recv(r, &reqid, sizeof(int), 0);
                ZCheck(rc);
                int64_t more = -1;
                size_t moreSize = sizeof(more);
                rc = zmq_getsockopt(r, ZMQ_RCVMORE, &more, &moreSize);
                ZCheck(rc);
                if(!more) rep = service_.Invoke(reqid, ByteArray());
                else {
                    rc = zmq_recv(r, args.data(), args.size(), 0);
                    ZCheck(rc);
                    rep = service_.Invoke(reqid, args);
                }
                ZCheck(zmq_send(r, &id[0], irc, ZMQ_SNDMORE));
                ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                ZCheck(zmq_send(r, &rep[0], rep.size(), 0));
            }
        }
        ZCleanup(ctx, r);
    }
    void Stop() { status_ = STOPPED; }
private:
    std::string uri_;
    Status status_ = STOPPED;
    ServiceImpl service_;
};

//MakeService
template < int... > struct IndexSequence {};
template < int M, int... Ints >
struct MakeIndexSequence : MakeIndexSequence< M - 1, M - 1, Ints...> {};

template < int... Ints >
struct MakeIndexSequence< 0, Ints... >  {
    using Type = IndexSequence< Ints... >;
};

template < typename R, int...Ints, typename...ArgsT >
R CallHelper(std::function< R (ArgsT...) > f,
       std::tuple< ArgsT... > args,
       const IndexSequence< Ints... >& ) {
    return f(std::get< Ints >(args)...);
};


template < typename R, int...Ints, typename...ArgsT >
R Call(std::function< R (ArgsT...) > f,
       std::tuple< ArgsT... > args) {
    return CallHelper(f, args,
                      MakeIndexSequence< sizeof...(ArgsT) >::Type());
};


struct IMethod {
    virtual ByteArray Invoke(const ByteArray& args) = 0;
    ~IMethod(){}
};

template < typename R, typename...ArgsT >
class Method : IMethod {
public:
    Method(std::function< R (ArgsT...) > f) : f_(f) {}
    ByteArray Invoke(const ByteArray& args) {
        std::tuple< ArgsT... > params =
                UnPack< std::tuple< ArgsT... > >(begin(args));
        R ret = Call(f_, params);
        return Pack(ret)
    }
private:
    std::function< R (ArgsT...) > f_;
};

template < typename...ArgsT >
class Method< void, ArgsT... > : IMethod {
public:
    Method(std::function< void (ArgsT...) > f) : f_(f) {}
    ByteArray Invoke(const ByteArray& args) {
        std::tuple< ArgsT... > params =
                UnPack< std::tuple< ArgsT... > >(begin(args));
        return ByteArray();
    }
private:
    std::function< void (ArgsT...) > f_;
};

template < typename R, typename...ArgsT >
Method< R, ArgsT... > MakeMethod(std::function< R (ArgsT...) > f) {
    return Method< R, ArgsT... >(f);
};

class MethodImpl {
public:
    template < typename R, typename...ArgsT >
    MethodImpl(const Method< R, ArgsT... >& m)
            : method_(new Method< R, ArgsT... >(m)) {}
    ByteArray Invoke(const ByteArray& args) {
        return method_->Invoke(args);
    }
private:
    std::unique_ptr< IMethod > method_;
};


//ServiceManager

class ServiceManager {
public:
    ServiceManager(const char* URI) : stop_(false) {
        Start(URI);
    }
    ServiceManager() = delete;
    ServiceManager(const ServiceManager&) = delete;
    ServiceManager& operator=(const ServiceManager&) = delete;
    ServiceManager(ServiceManager&&) = default;
    void Add(const std::string& name,
             const Service& service) {
        services_[name] = service;
    }
    bool Exists(const std::string& s) const {
        return services_.find(s) != services_.end();
    }
    bool Started(const std::string& s) const {
        return serviceFutures_.find(s) != serviceFutures_.end();
    }
    void Stop() {
        stop_ = true;
    }
    ~ServiceManager() {
        Stop();
    }
    void Start(const char* URI) {
        void* ctx = zmq_ctx_new();
        void* r = zmq_socket(ctx, ZMQ_ROUTER);
        zmq_bind(r, URI);
        zmq_pollitem_t items[] = { { r, 0, ZMQ_POLLIN, 0 } };
        vector< char > id(10, char(0));
        ByteArray buffer(0x10000);
        while(!stop_) {
            zmq_poll(items, 1, 10); //poll with 100ms timeout
            //check for incoming messages and add them into worker queue:
            // <client id, request>
            if(items[0].revents & ZMQ_POLLIN) {
                const int irc = zmq_recv(r, &id[0], id.size(), 0);
                ZCheck(irc);
                ZCheck(zmq_recv(r, 0, 0, 0));
                const int rc = zmq_recv(r, &buffer[0], buffer.size(), 0);
                ZCheck(rc);
                const std::string serviceName = UnPack< std::string >(buffer);
                if(!Exists(serviceName)) {
                    const std::string error =
                            "No " + serviceName + " available";
                    ByteArray rep = Pack(error);
                    ZCheck(zmq_send(r, &id[0], irc, ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, &rep[0], rep.size(), 0));
                } else {
                    //we need to get a reference to the service in order
                    //not to access the map from a separate thread
                    Service& service = this->services_[serviceName];
                    auto executeService = [this](const std::string& name,
                                                 Service* pservice) {
                        pservice->Start();
                    };
                    auto f = std::async(std::launch::async,
                                        executeService,
                                        &service);
                    serviceFutures_[serviceName] = std::move(f);
                    ByteArray rep = Pack(service.GetURI());
                    ZCheck(zmq_send(r, &id[0], irc, ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, &rep[0], rep.size(), 0));
                }
            }
        }
        ZCleanup(ctx, r);
        StopServices();
    }
    void StopServices() {
        std::map< std::string, Service >::iterator si
                = services_.begin();
        for(si != services_.end(); ++si) {
            si->second.Stop();
        }
        std::map< std::string, std::future< void > >::iterator fi
                = serviceFutures_.begin();
        for(fi != serviceFutures_.end(); ++fi) {
            fi->second.get(); //get propagates exceptions, wait does not
        }
    }
private:
    bool stop_;
    std::map< std::string, Service > services_;
    std::map< std::string, std::future< void > > serviceFutures_;
};


//ServiceProxy
///@todo ServiceProxy

class ServiceProxy {
public:
    ServiceProxy() = delete;
    ServiceProxy(const ServiceProxy&) = delete;
    ServiceProxy(ServiceProxy&&) = default;
    ServiceProxy& operator=(const ServiceProxy&) = delete;
    ServiceProxy(const char* serviceManagerURI, const char* serviceName) {
        Connect(GetServiceURI(serviceManagerURI, serviceName));
    }
    RemoteInvoker operator[](int id) {
        return RemoteInvoker(this, id);
    }
    template < typename R, typename...ArgsT >
    R Request(ArgsT...args) {
        sendBuf_.resize(0);
        recvBuf_.resize(0);
        sendBuf_ = Pack(make_tuple(args...), std::move(sendBuf_));
        Send();
        return UnPack< R >(begin(recvBuf_));

    };
    ~ServiceProxy() {
        ZCleanup(ctx_, serviceSocket_);
    }
private:
    struct ByteArrayWrapper {
        ByteArrayWrapper(ByteArray&& ba) : ba_(std::move(ba)) {}
        template < typename T >
        operator T() {
            T d;
            UnPack(begin(ba_), d;
            return d;
        }
        ByteArray ba_;
    };
    template < typename R >
    inline R& operator=(R& r, const ByteArrayWrapper& baw) {
        UnPack(begin(baw.ba_), r);
        return r;
    }
    friend class RemoteInvoker {
    public:
        RemoteInvoker(ServiceProxy* sp, int reqid) :
                sp_(sp), reqid_(reqid) {}
        template < typename...ArgsT >
        ByteArrayWrapper operator()(ArgsT...args) {
            sp_->sendBuf_.resize(0);
            sp_->recvBuf_.resize(0);
            sendBuf_ = Pack(make_tuple(args...), std::move(sendBuf_));
            Send();
            return recvBuf_;
        }
    private:
        ServiceProxy sp_;
        int reqid_;
    };
    std::string GetServiceURI(const char* serviceManagerURI,
                              const char* serviceName) {
        void* tmpCtx = zmq_ctx_new();
        assert(tmpCtx);
        void* tmpSocket = zmq_socket(tmpCtx, ZMQ_REQ);
        assert(tmpSocket);
        ZCheck(zmq_connect(tmpSocket, serviceManagerURI));
        ZCheck(zmq_send(tmpSocket, serviceName, strlen(serviceName), 0));
        ByteArray rep(0x10000);
        int rc = zmq_recv(tmpSocket, rep.data(), rep.size(), 0);
        ZCheck(rc);
        ZCleanup(tmpCtx, tmpSocket);
        return std::string(begin(rep), begin(rep) + rc);
    }
    void Connect(const std::string& serviceURI) {
        ctx_ = zmq_ctx_new();
        assert(ctx_);
        serviceSocket_ = zmq_socket(ctx_, ZMQ_REQ);
        assert(serviceSocket_);
        ZCheck(zmq_connect(serviceSocket_, serviceURI.c_str()));
    }
private:
    void Send() {
        ZCheck(zmq_send(serviceSocket_, sendBuf_.data(), sendBuf_.size(), 0));
        ZCheck(zmq_recv(serviceSocket_, recvBuf_.data(), recvBuf_.size(), 0));
    }
private:
    ByteArray sendBuf_;
    ByteArray recvBuf_;
    void* ctx_;
    void* serviceSocket_;
};


//------------------------------------------------------------------------------

using namespace std;
int main(int, char**) {
    //FileService
    enum {FS_LS = 1};
    ServiceImpl si;
    si.Add(FS_LS, MakeMethod([](const std::string& dir) {
        return std::vector< string >{"1", "2", "three"};}));
    Service fs("ipc://file-service", si);
    //Add to service manager
    ServiceManager sm;
    sm.Add("file service", fs);
    //Start service manager in separate thread
    auto s = async(launch::async, sm.Start("ipc://service-manager"));

    //Client
    ServiceProxy sp("ipc://service-manager", "file service");
    //Execute remote method
    vector< string > lsresult = sp[FS_LS]("/");
    copy(begin(lsresult), end(lsresult),
         ostream_iterator< string >(cout, "\n"));

    return EXIT_SUCCESS;
}