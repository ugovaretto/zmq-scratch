//
// Created by Ugo Varetto on 7/4/16.
//
#include <zmq.h>
#include <memory>
#include <string>
#include <iterator>
#include <vector>

#include "Serialize.h





//IService

struct IService {
    std::vector< char > Invoke(int id, const std::vector< char >& params) = 0;
    ~IService();
};

//Service

class Service {
public:
    enum Status {STOPPED, STARTED};
public:
    Service(const char* URI, IService* ps) : service_(ps) {}
    Status GetStatus() const  { return status_ }
private:
    Status status_ = STOPPED;
    std::unique_ptr< IService > service_;
};

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

//ServiceManager

class ServiceManager {
public:
    ServiceManager(const char* URI) {
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
private:
    void Start(const char* URI) {
        void* ctx = zmq_ctx_new();
        void* r = zmq_socket(ctx, ZMQ_ROUTER);
        zmq_bind(r, URI);
        zmq_pollitem_t items[] = { { r, 0, ZMQ_POLLIN, 0 } };
        while(true) {
            int rc = zmq_poll(items, 1, -1);
            //recv string
            //if string matches service name:
            //  start service
            //  reply with service URI
            //else
            //  reply with error string
        }
    }
    string StartService(const std::string& name) {
        const std::string uri = GetURI();
        auto f = [this, name](const std::string& uri) {
            Service s = services_[name];
            s.Start(uri);
        };
        serviceFutures_[name].push_back(
                std::async(std::launch::async, f, uri));

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