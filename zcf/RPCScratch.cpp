//
// Created by Ugo Varetto on 7/4/16.
//

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

struct IService {
    virtual std::vector< char > Invoke(int id,
                                       const std::vector< char >& params) = 0;

    virtual ~IService() {};
};

//Service

class Service {
public:
    enum Status {STOPPED, STARTED};
public:
    Service(const std::string& URI, IService* ps)
            : status_(STOPPED), uri_(URI), service_(ps) {}
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
            zmq_poll(items, 1, 20); //poll with 100ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                const int irc = zmq_recv(r, &id[0], id.size(), 0);
                ZCheck(irc);
                ZCheck(zmq_recv(r, 0, 0, 0));
                int rc = zmq_recv(r, &reqid, sizeof(int), 0);
                ZCheck(rc);
                int64_t more = -1;
                size_t moreSize = sizeof(more);
                rc = zmq_getsockopt (r, ZMQ_RCVMORE, &more, &moreSize);
                ZCheck(rc);
                if(!more) rep = service_->Invoke(reqid, ByteArray());
                else {
                    rc = zmq_recv(r, args.data(), args.size(), 0);
                    ZCheck(rc);
                    rep = service_->Invoke(reqid, args);
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
    std::unique_ptr< IService > service_;
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


//------------------------------------------------------------------------------

//FileService

enum {FS_LS = 1};

class FileService : public IService {
public:
    std::vector< char > Invoke(int id, const std::vector< char >& params) {
        switch(id) {
            case FS_LS:
                std::string s;
                GetSerializer< std::string >::Type::UnPack(params, s);
                std::vector< std::string > result = {"/a", "/b", "/c"};
                return GetSerializer< std::vector< std::string > >::Type
                ::Pack(result);
            default: return std::vector< string >{"ERROR"};
        }
    }
};

using namespace std;


int main(int, char**) {

    Service fs("ipc://file-service", new FileService);
    ServiceManager sm;
    sm.Add("file service", fs);

    auto s = async(launch::async, sm.Start("ipc://service-manager"));

    ServiceProxy sp("ipc://service-manager", "file service");

    vector< string > lsresult = sp[FS_LS]("/");

    copy(begin(lsresult), end(lsresult),
         ostream_iterator< string >(cout, "\n"));

    return EXIT_SUCCESS;
}