//
// Created by Ugo Varetto on 7/4/16.
//
#include <zmq.h>
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