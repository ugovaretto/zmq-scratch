//
// Created by Ugo Varetto on 7/4/16.
//

//Remote method invocation implementation

///@todo cleanup, tests with asserts

///@todo resize receive buffer before and after recv

///@todo error handling

///@todo should I make RemobeInvoker accessible ? In this case the buffer
///has to be copied

///@todo typedef req id

///@todo Make type safe: no check is performed when de-serializing
///consider optional addition of type information for each serialized type
///return method signature and description from service
///add ability to interact with service manager asking for supported services
///consider having service manager select URI based on workload, can be local
///or remote, in this case a protocol must be implemented to allow service
///managers to talk to each other and exchange information about supported
///services and current workload

///@todo consider using TypedSerializers

///@todo need a way to determine if service is under stress e.g. interval
///between end of request and start of new one, num requests/s...

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
#include <functional>

#include "Serialize.h"
#include "SyncQueue.h"

///@todo use fixed number of workers or thread pool

//==============================================================================
//UTILITY
//==============================================================================
#define LOG__

void Log(const std::string& msg) {
#ifdef LOG__
    std::cout << msg << std::endl;
#endif
}

//------------------------------------------------------------------------------
//ZEROMQ ERROR HANDLERS
void ZCheck(int v) {
    if(v < 0) {
        throw std::runtime_error(std::string("ZEROMQ ERROR: ")
                                 + strerror(errno));
    }
}

void ZCheck(void* p) {
    if(!p) {
        throw std::runtime_error("ZEROMQ ERROR: null pointer");
    }
}

void ZCleanup(void *context, void *zmqsocket) {
    ZCheck(zmq_close(zmqsocket));
    ZCheck(zmq_ctx_destroy(context));
}

//------------------------------------------------------------------------------
//Template Meta Programming
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


template < typename R,  int...Ints, typename...ArgsT >
R Call(std::function< R (ArgsT...) > f,
       std::tuple< ArgsT... > args) {
    return CallHelper(f, args,
                      typename MakeIndexSequence< sizeof...(ArgsT) >::Type());
};

template < typename R, int...Ints, typename...ArgsT >
R MoveCallHelper(std::function< R (ArgsT...) > f,
             std::tuple< ArgsT... >&& args,
             const IndexSequence< Ints... >& ) {
    return f(std::move(std::get< Ints >(args))...);
};


template < typename R,  int...Ints, typename...ArgsT >
R MoveCall(std::function< R (ArgsT...) > f,
       std::tuple< ArgsT... >&& args) {
    return MoveCallHelper(f, std::move(args),
                      typename MakeIndexSequence< sizeof...(ArgsT) >::Type());
};

template < typename T >
struct RemoveAll {
    using Type = typename std::remove_cv<
            typename std::remove_reference< T >::type >::type;
};

//==============================================================================
//METHOD
//==============================================================================
struct IMethod {
    virtual ByteArray Invoke(const ByteArray& args) = 0;
    virtual IMethod* Clone() const = 0;
    virtual ~IMethod(){}
};

struct EmptyMethod : IMethod {
    ByteArray Invoke(const ByteArray& ) { return ByteArray();}
    virtual IMethod* Clone() const { return new EmptyMethod; };
};


template < typename R, typename...ArgsT >
class Method : public IMethod {
public:
    Method() = default;
    Method(const std::function< R (ArgsT...) >& f) : f_(f) {}
    Method(const Method&) = default;
    Method* Clone() const { return new Method< R, ArgsT... >(*this); }
    ByteArray Invoke(const ByteArray& args) {
        //std::tuple< typename RemoveAll< ArgsT >::Type... > params =
        std::tuple< ArgsT... > params =
                UnPack< std::tuple<
                        typename RemoveAll< ArgsT >::Type... > >(begin(args));
        R ret = MoveCall(f_, std::move(params));
        return Pack(ret);
    }
private:
    std::function< R (ArgsT...) > f_;
};

template < typename...ArgsT >
class Method< void, ArgsT... > : public IMethod {
public:
    Method() = default;
    Method(const std::function< void (ArgsT...) >& f) : f_(f) {}
    Method(const Method&) = default;
    Method* Clone() const { return new Method< void, ArgsT... >(*this); }
    ByteArray Invoke(const ByteArray& args) {
        //std::tuple< typename RemoveAll< ArgsT >::Type... > params =
        std::tuple< ArgsT... > params =
                UnPack< std::tuple<
                        typename RemoveAll< ArgsT >::Type... > >(begin(args));
        MoveCall(f_, std::move(params));
        return ByteArray();
    }
private:
    std::function< void (ArgsT...) > f_;
};


template < typename R >
class Method< R > : public IMethod {
public:
    Method() = default;
    Method(const std::function< R () >& f) : f_(f) {}
    Method(const Method&) = default;
    Method* Clone() const { return new Method< R >(*this); }
    ByteArray Invoke(const ByteArray&) {
        R ret = f_();
        return Pack(ret);
    }
private:
    std::function< R () > f_;
};

template <>
class Method< void > : public IMethod {
public:
    Method() = default;
    Method(const std::function< void () >& f) : f_(f) {}
    Method(const Method&) = default;
    Method* Clone() const { return new Method< void >(*this); }
    ByteArray Invoke(const ByteArray&) {
        f_();
        return ByteArray();
    }
private:
    std::function< void () > f_;
};

class MethodImpl {
public:
    MethodImpl() : method_(new EmptyMethod) {}
    MethodImpl(IMethod* pm) : method_(pm) {}
    MethodImpl(const MethodImpl& mi) :
            method_(mi.method_ ? mi.method_->Clone() : nullptr) {}
    MethodImpl& operator=(const MethodImpl& mi) {
        method_.reset(mi.method_->Clone());
        return *this;
    }
    template < typename R, typename...ArgsT >
    MethodImpl(const Method< R, ArgsT... >& m)
            : method_(new Method< R, ArgsT... >(m)) {}
    template < typename R, typename...ArgsT >
    MethodImpl(const std::function< R (ArgsT...) >& f)
            : MethodImpl(Method< R, typename RemoveAll< ArgsT >::Type...>(f))
    {};

    ByteArray Invoke(const ByteArray& args) {
        return method_->Invoke(args);
    }
private:
    std::unique_ptr< IMethod > method_;
};

//template < typename R, typename...ArgsT >
//MethodImpl MakeMethod(const std::function< R (ArgsT...) >& f) {
//    return MethodImpl(Method< R, ArgsT... >(f));
//};

//==============================================================================
//SERVICE
//==============================================================================
///@todo should the method id be an int ? typedef ? template parameter ?

//Actual service implementation
class ServiceImpl {
public:
    void Add(int id, const MethodImpl& mi) {
        methods_[id] = mi;
    }
    template < typename R, typename...ArgsT >
    void Add(int id, const std::function< R (ArgsT...) >& f) {
        Add(id, MethodImpl(f));
    };
    ByteArray Invoke(int reqid, const ByteArray& args) {
        return methods_[reqid].Invoke(args);
    }
private:
    std::map< int, MethodImpl > methods_;
};

//Service wrapper
enum {SERVICE_ERROR = -1, SERVICE_NO_ERROR = 0};
class Service {
public:
    enum Status {STOPPED, STARTED};
public:
    Service() = default;
    Service(const std::string& URI, const ServiceImpl& si)
            : status_(STOPPED), uri_(URI), service_(si) {}
    Status GetStatus() const  { return status_; }
    std::string GetURI() const {
        return uri_;
    }
    void Start() {
        status_ = STARTED;
        void* ctx = zmq_ctx_new();
        ZCheck(ctx);
        void* r = zmq_socket(ctx, ZMQ_ROUTER);
        ZCheck(r);
        ZCheck(zmq_bind(r, uri_.c_str()));
        zmq_pollitem_t items[] = { { r, 0, ZMQ_POLLIN, 0 } };
        int reqid = -1;
        ByteArray args(0x10000); ///@todo configurable buffer size
        ByteArray rep;
        std::vector< char > id(10, char(0));
        while(status_ != STOPPED) {
            zmq_poll(items, 1, 20); //poll with 20ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                const int irc = zmq_recv(r, &id[0], id.size(), 0);
                ZCheck(irc);
                ZCheck(zmq_recv(r, 0, 0, 0));
                int rc = zmq_recv(r, &reqid, sizeof(int), 0);
                ZCheck(rc);
                Log("service>> request id: " + std::to_string(reqid));
                int64_t more = 0;
                size_t moreSize = sizeof(more);
                rc = zmq_getsockopt(r, ZMQ_RCVMORE, &more, &moreSize);
                ZCheck(rc);
                try {
                    if(!more) rep = service_.Invoke(reqid, ByteArray());
                    else {
                        rc = zmq_recv(r, args.data(), args.size(), 0);
                        ZCheck(rc);
                        Log("service>> request data received");
                        rep = service_.Invoke(reqid, args);
                        Log("service>> request executed");
                    }
                    ZCheck(zmq_send(r, &id[0], size_t(irc), ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    int okStatus = SERVICE_NO_ERROR;
                    ZCheck(zmq_send(r, &okStatus, sizeof(okStatus),
                                    ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, rep.data(), rep.size(), 0));
                    Log("service>> reply sent");
                } catch(const std::exception& e) {
                    const std::string msg = e.what();
                    ZCheck(zmq_send(r, &id[0], size_t(irc), ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    int errorStatus = SERVICE_ERROR;
                    ZCheck(zmq_send(r, &errorStatus, sizeof(errorStatus),
                                    ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, rep.data(), rep.size(), 0));

                }
            }
        }
        ZCleanup(ctx, r);
        Log("service>> " + uri_ + " stopped");
    }
    void Stop() { status_ = STOPPED; } //invoke from separate thread
private:
    std::string uri_;
    Status status_ = STOPPED;
    ServiceImpl service_;
};


//==============================================================================
//SERVICE MANAGER
//==============================================================================
class ServiceManager {
public:
    ServiceManager(const char* URI) : stop_(false) {
        Start(URI);
    }
    ServiceManager() : stop_(false) {}
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
    //returns true if a service was started some time in the past,
    //will keep to return true even after it stops
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
        stop_ = false;
        void* ctx = zmq_ctx_new();
        ZCheck(ctx);
        void* r = zmq_socket(ctx, ZMQ_ROUTER);
        ZCheck(r);
        ZCheck(zmq_bind(r, URI));
        zmq_pollitem_t items[] = { { r, 0, ZMQ_POLLIN, 0 } };
        std::vector< char > id(10, char(0));
        ByteArray buffer(0x10000);
        while(!stop_) {
            ZCheck(zmq_poll(items, 1, 100)); //poll with 100ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                const int irc = zmq_recv(r, &id[0], id.size(), 0);
                ZCheck(irc);
                ZCheck(zmq_recv(r, 0, 0, 0));
                const int rc = zmq_recv(r, &buffer[0], buffer.size(), 0);
                ZCheck(rc);
                const std::string serviceName
                        = UnPack< std::string >(begin(buffer));
                Log("server>> " + serviceName + " requested");
                if(!Exists(serviceName)) {
                    const std::string error =
                            "No " + serviceName + " available";
                    ByteArray rep = Pack(error);
                    ZCheck(zmq_send(r, &id[0], size_t(irc), ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, &rep[0], rep.size(), 0));
                } else {
                    //we need to get a reference to the service in order
                    //not to access the map from a separate thread
                    Service& service = this->services_[serviceName];
                    auto executeService = [](Service* pservice) {
                        pservice->Start();
                    };
                    auto f = std::async(std::launch::async,
                                        executeService,
                                        &service);
                    Log("server>> Started service at " + service.GetURI());
                    serviceFutures_[serviceName] = std::move(f);
                    ByteArray rep = Pack(service.GetURI());
                    ZCheck(zmq_send(r, &id[0], size_t(irc), ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, rep.data(), rep.size(), 0));
                }
            }
        }
        ZCleanup(ctx, r);
        StopServices();
        Log("server>> stopped");
    }
    void StopServices() {
        std::map< std::string, Service >::iterator si
                = services_.begin();
        for(;si != services_.end(); ++si) {
            si->second.Stop();
        }
        std::map< std::string, std::future< void > >::iterator fi
                = serviceFutures_.begin();
        for(;fi != serviceFutures_.end(); ++fi) {
            fi->second.get(); //get propagates exceptions, wait does not
        }
    }
private:
    bool stop_;
    std::map< std::string, Service > services_;
    std::map< std::string, std::future< void > > serviceFutures_;
};


///=============================================================================
//SERVICE CLIENT PROXY
//==============================================================================
///@todo should I call it ServiceClient instead of ServiceProxy ?

bool ServiceError(int status) {
    return status == SERVICE_ERROR;
}

struct RemoteServiceException : std::exception {
    RemoteServiceException(const std::string& msg) : msg_(msg) {}
    const char* what() const noexcept {
        return msg_.c_str();
    }
private:
    std::string msg_;
};


class ServiceProxy {
private:
    struct ByteArrayWrapper {
        ByteArrayWrapper(const ByteArray& ba) : ba_(std::cref(ba)) {}
        ByteArrayWrapper(const ByteArrayWrapper&) = default;
        template < typename T >
        operator T() const {
            return To< T >(ba_);
        }
        std::reference_wrapper< const ByteArray > ba_;
    };

    class RemoteInvoker {
    public:
        RemoteInvoker(ServiceProxy *sp, int reqid) :
                sp_(sp), reqid_(reqid) { }

        template<typename...ArgsT>
        const ByteArrayWrapper operator()(ArgsT...args) {
            sp_->sendBuf_.resize(0);
            sp_->sendBuf_ = Pack(std::make_tuple(args...),
                                 std::move(sp_->sendBuf_));
            sp_->Send(reqid_);
            return ByteArrayWrapper(sp_->recvBuf_);
        }
        const ByteArrayWrapper operator()() {
            sp_->sendBuf_.resize(0);
            sp_->sendBuf_.resize(0);
            sp_->Send(reqid_);
            return ByteArrayWrapper(sp_->recvBuf_);
        }
    private:
        ServiceProxy* sp_;
        int reqid_;
    };
    friend class RemoteInvoker;
public:
    ServiceProxy() = delete;
    ServiceProxy(const ServiceProxy&) = delete;
    ServiceProxy(ServiceProxy&&) = default;
    ServiceProxy& operator=(const ServiceProxy&) = delete;
    ServiceProxy(const char* serviceManagerURI, const char* serviceName) :
            recvBuf_(0x1000) {
        Connect(GetServiceURI(serviceManagerURI, serviceName));
    }
    RemoteInvoker operator[](int id) {
        return RemoteInvoker(this, id);
    }
    template < typename R, typename...ArgsT >
    R Request(int reqid, ArgsT...args) {
        sendBuf_.resize(0);
        sendBuf_ = Pack(std::make_tuple(args...), std::move(sendBuf_));
        Send(reqid);
        return UnPack< R >(begin(recvBuf_));

    };
    ~ServiceProxy() {
        ZCleanup(ctx_, serviceSocket_);
    }
private:

    std::string GetServiceURI(const char* serviceManagerURI,
                              const char* serviceName) {
        void* tmpCtx = zmq_ctx_new();
        ZCheck(tmpCtx);
        void* tmpSocket = zmq_socket(tmpCtx, ZMQ_REQ);
        ZCheck(tmpSocket);
        ZCheck(zmq_connect(tmpSocket, serviceManagerURI));
        ByteArray req = Pack(std::string(serviceName));
        ZCheck(zmq_send(tmpSocket, req.data(), req.size(), 0));
        ByteArray rep(0x10000);
        int rc = zmq_recv(tmpSocket, rep.data(), rep.size(), 0);
        ZCheck(rc);
        ZCleanup(tmpCtx, tmpSocket);
        return To< std::string >(rep);
    }
    void Connect(const std::string& serviceURI) {
        ctx_ = zmq_ctx_new();
        ZCheck(ctx_);
        serviceSocket_ = zmq_socket(ctx_, ZMQ_REQ);
        ZCheck(serviceSocket_);
        ZCheck(zmq_connect(serviceSocket_, serviceURI.c_str()));
        Log("client>> connected to " + serviceURI);
    }
private:
    void Send(int reqid) {
        if(!sendBuf_.empty()) {
            ZCheck(zmq_send(serviceSocket_, &reqid, sizeof(reqid),
                            ZMQ_SNDMORE));
            ZCheck(zmq_send(serviceSocket_, sendBuf_.data(), sendBuf_.size(),
                            0));
        } else {
            ZCheck(zmq_send(serviceSocket_, &reqid, sizeof(reqid),
                            0));
        }
        Log("client>> sent data");
        int status = -1;
        ZCheck(zmq_recv(serviceSocket_, &status, sizeof(status), 0));
        int64_t more = 0;
        size_t moreSize = sizeof(more);
        ZCheck(zmq_getsockopt(serviceSocket_, ZMQ_RCVMORE, &more, &moreSize));
        if(more) {
            ZCheck(zmq_recv(serviceSocket_, recvBuf_.data(),
                            recvBuf_.size(), 0));
        }
        if(ServiceError(status)) {
            std::string errorMsg = "Service Error";
            if(more) {
                errorMsg += ": " + To< std::string >(recvBuf_);
                throw RemoteServiceException(errorMsg);
            }
        }
        Log("client>> received data");
    }
private:
    ByteArray sendBuf_;
    ByteArray recvBuf_;
    void* ctx_;
    void* serviceSocket_;
};


//==============================================================================
//DRIVER
//==============================================================================
using namespace std;

int main(int, char**) {

    //FileService
    ServiceImpl si;
    struct FSMethod : IMethod {
        ByteArray Invoke(const ByteArray& args) {
            tuple< string > arg = To< tuple< string > >(args);
            const string dir = get< 0 >(arg);
            Log("method>> args: " + dir);
            const std::vector< string > rep = {dir + "/1", dir + "/2"};
            return Pack(rep);
        }
        IMethod* Clone() const {
            return new FSMethod;
        }
    };
    //Sum
    //Method< int, int, int > sum([](const int& i1, int i2) { return i1 + i2;});
    MethodImpl mi(std::function< int (const int&, const int&) >(
            [](const int& i1, const int& i2) -> int { return i1 + i2;}));

    enum {FS_LS = 1, SUM, EXCEPTIONAL, PI};
    si.Add(FS_LS, MethodImpl(new FSMethod));
    //si.Add(SUM, mi);
    si.Add(SUM, std::function< int (const int&, const int&) >(
            [](const int& i1, const int& i2) -> int { return i1 + i2;}));
    si.Add(EXCEPTIONAL, std::function< void () >([](){throw std::runtime_error("EXCEPTION");}));
    si.Add(PI, std::function< double () >([](){ return 3.14159265358979323846; }));
    Service fs("ipc://file-service", si);
    //Add to service manager
    ServiceManager sm;
    sm.Add("file service", fs);
    //Start service manager in separate thread
    auto s = async(launch::async, [&sm](){sm.Start("ipc://service-manager");});

    //Client
    ServiceProxy sp("ipc://service-manager", "file service");
    //Execute remote methods
    const vector< string > lsresult =
            sp.Request< decltype(lsresult) >(FS_LS, string("mydir"));
    copy(begin(lsresult), end(lsresult),
         ostream_iterator< string >(cout, "\n"));
    const int sumresult = sp.Request< int >(SUM, 5, 4);
    int ss = sp[SUM](7,4);
    try {
        sp[EXCEPTIONAL]();
    } catch(const RemoteServiceException& e) {
        cout << "Remote Service Exception: " << e.what() << endl;
    }
    const double MPI = sp[PI]();
    cout << "7 + 4 = " << ss << endl;
    cout << "5 + 4 = " << sumresult << endl;
    cout << "PI    = " << MPI << endl;
    sm.Stop();
    return EXIT_SUCCESS;
}