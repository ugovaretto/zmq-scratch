//  Broker peering simulation (part 2)
//  Prototypes the request-reply flow

#include <iostream>
#include <string>
#include <future>
#include <sstream>
#include <deque>
#include <vector>
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif

#define NBR_CLIENTS 10
#define NBR_WORKERS 3
#define WORKER_READY   "\001"      //  Signals worker is ready


//  .split client task
//  The client task does a request-reply dialog using a standard
//  synchronous REQ socket:


std::vector< char > recv_all(void* socket) {
    int rc = -1;
    bool finished = false;
    int64_t opt = 0;
    std::vector< char > ret;
    std::vector< char > buffer(0x100);
    while(!finished) {
        zmq_getsockopt(socket, ZMQ_RECVMORE, &opt, sizeof(opt));
        if(opt) {
            rc = zmq_recv(socket, &buffer[0], buffer.size(), 0);
            if(rc < 0) break;
            std::copy(buffer.begin(), buffer.begin() + rc,
                      std::back_inserter(ret));
        } else finished = true;

   return ret;
}

std::vector< std::vector< char > >
recv_messages(void* socket) {
    int rc = -1;
    bool finished = false;
    int64_t opt = 0;
    std::vector< std::vector< char > > ret;
    std::vector< char > buffer(0x100);
    while(!finished) {
        zmq_getsockopt(socket, ZMQ_RECVMORE, &opt, sizeof(opt));
        if(opt) {
            rc = zmq_recv(socket, &buffer[0], buffer.size(), 0);
            if(rc < 0) break;
            buffer.resize(rc);
            ret.push_back(buffer);
        } else finished = true;

   return ret;
}

std::vector< std::vector< char > >
send_messages(void* socket,
              const std::vector< std::vector< char > >& msgs) {
   std::for_each(msgs.begin(), --msgs.end(), 
                [socket](const std::vector< char >& msg){
       zmq_send(socket, &msg[0], msg.size(), ZMQ_SNDMORE);  
   });
   zmq_send(socket, &(msg.back()[0]), msg.back().size(), 0);
   return ret;
}


std::string make_id(const std::string& id, int num) {
    std::ostringstream oss;
    oss << id << ":" << num;
    return oss.str();
}

void client_task(const std::string& id, int num)
{
    zctx_t *ctx = zctx_new ();
    void *client = zsocket_new (ctx, ZMQ_REQ);
    zsocket_connect (client, "ipc://%s-localfe.ipc", self);

    while (true) {
        //  Send request, get reply
        zstr_send (client, "HELLO");
        char *reply = zstr_recv (client);
        if (!reply)
            break;              //  Interrupted
        printf ("Client: %s\n", reply);
        free (reply);
        sleep (1);
    }
    zctx_destroy (&ctx);
    return NULL;
}

//  .split worker task
//  The worker task plugs into the load-balancer using a REQ
//  socket:

void worker_task(const std::string& id, int num)
{
    zctx_t *ctx = zctx_new ();
    void *worker = zsocket_new (ctx, ZMQ_REQ);
    zsocket_connect (worker, "ipc://%s-localbe.ipc", self);

    //  Tell broker we're ready for work
    zframe_t *frame = zframe_new (WORKER_READY, 1);
    zframe_send (&frame, worker, 0);

    //  Process messages as they arrive
    while (true) {
        zmsg_t *msg = zmsg_recv (worker);
        if (!msg)
            break;              //  Interrupted

        zframe_print (zmsg_last (msg), "Worker: ");
        zframe_reset (zmsg_last (msg), "OK", 2);
        zmsg_send (&msg, worker);
    }
    zctx_destroy (&ctx);
    return NULL;
}

//  .split main task
//  The main task begins by setting-up its frontend and backend sockets
//  and then starting its client and worker tasks:

int main (int argc, char *argv [])
{
    //  First argument is this broker's name
    //  Other arguments are our peers' names
    //
    if (argc < 2) {
        printf ("syntax: peering2 me {you}...\n");
        return 0;
    }
    const std::string self = argv [1];
    std::cout << "I: preparing broker at " << self << << std::endl;
    void* ctx = zmq_ctx_new();

    //  Bind cloud frontend to endpoint
    void* cloudfe = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_setsockopt(cloudfe, ZMQ_IDENTITY, self.c_str(), self.size());
    zsocket_bind(cloudfe, ("ipc://" + self + "-cloud.ipc").c_str());

    //  Connect cloud backend to all peers
    void* cloudbe = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_setsockopt(cloudfe, ZMQ_IDENTITY, self.c_str(), self.size());
    int argn;
    for (argn = 2; argn < argc; argn++) {
        std::string peer = argv [argn];
        std::cout << "I: connecting to cloud frontend at '" 
                  << peer << "'\n";
        zmq_connect(cloudbe, ("ipc://" + peer + "-cloud.ipc").c_str());
    }
    //  Prepare local frontend and backend
    void *localfe = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_bind(localfe, ("ipc://" + self + "-localfe.ipc").c_str());
    void *localbe = zmq_socket(ctx, ZMQ_ROUTER);
    zmq_bind(localbe, ("ipc://" + self + "-localbe.ipc").c_str());

    //  Get user to tell us when we can start...
    std::cout << "Press Enter when all brokers are started" << std::endl;
    const int ready = std::cin.get();

    typedef std::vector< std::future< void > > FutureArray;
    FutureArray clients;
    FutureArray servers;
    //  Start local workers
    for(int worker_nbr = 0; worker_nbr < NBR_WORKERS; worker_nbr++)
        workers.push_back(
            std::move(
                std::async(std::launch::async,
                           worker_task, self, worker_nbr + 1)));

   
    for(int client_nbr = 0; client_nbr < NBR_CLIENTS; client_nbr++)
         clients.push_back(
            std::move(
                std::async(std::launch::async,
                           client_task, self, client_nbr + 1)));

    //  .split request-reply handling
    //  Here, we handle the request-reply flow. We're using load-balancing
    //  to poll workers at all times, and clients only when there are one 
    //  or more workers available.

    std::deque< std::string > worker_queue;

    while (true) {
        //  First, route any waiting replies from workers
        zmq_pollitem_t backends [] = {
            { localbe, 0, ZMQ_POLLIN, 0 },
            { cloudbe, 0, ZMQ_POLLIN, 0 }
        };
        //  If we have no workers, wait indefinitely
        int rc = zmq_poll (backends, 2,
            worker_queue ? 100: -1);
        if (rc == -1)
            break;              //  Interrupted

        //  Handle reply from local worker
        std::vector< char > msg(0x200);
        void* dest_socket = 0;
        std::string dest_id;
        if(backends[0].revents & ZMQ_POLLIN) {
            //recv ID
            rc = zmq_recv(localbe, &msg, msg.size(), 0);
            if (!msg)
                break;
            msg[rc] = '\0'; //  Interrupted
            if(std::string(&msg[0]) == WORKER_READY) {
                worker_queue.push_back(std::string(&msg[0]));
            } else {
                //dest id
                rc = zmq_recv(localbe, &msg[0], msg.size(), 0);
                msg[rc] = '\0';
                dest_id = &msg[0];
                if(is_peer_name(&msg[0]), argv) {
                    dest_socket = cloudfe;
                } else {   
                    dest_socket = localfe;
                }
                msg = recv_all(socket);
            }
        }  //  Or handle reply from peer broker
        else if(backends [1].revents & ZMQ_POLLIN) {
            //back end id - discard
            rc = zmq_recv(cloudbe, &msg, msg.size(), 0);
            //destination id
            rc = zmq_recv(cloudbe, &msg, msg.size(), 0);
            dest_id = &msg[0];
            if(is_peer_name(&msg[0]), argv) {
                dest_socket = cloudfe;
            } else {   
                dest_socket = localfe;
            }
            mesg = recv_all(socket);
        }
        //  Route reply to client if we still need to
        if(dest_socket != 0)
            zmq_send(dest_socket, &msg[0], msg.size());

        //  .split route client requests
        //  Now we route as many client requests as we have worker capacity
        //  for. We may reroute requests from our local frontend, but not from 
        //  the cloud frontend. We reroute randomly now, just to test things
        //  out. In the next version, we'll do this properly by calculating
        //  cloud capacity:

        while(worker_queue.size()) {
            zmq_pollitem_t frontends [] = {
                { localfe, 0, ZMQ_POLLIN, 0 },
                { cloudfe, 0, ZMQ_POLLIN, 0 }
            };
            rc = zmq_poll (frontends, 2, 0);
            assert (rc >= 0);
            int reroutable = 0;
            //  We'll do peer brokers first, to prevent starvation
            if (frontends [1].revents & ZMQ_POLLIN) {
                msg = zmsg_recv (cloudfe);
                reroutable = 0;
            }
            else
            if (frontends [0].revents & ZMQ_POLLIN) {
                msg = zmsg_recv (localfe);
                reroutable = 1;
            }
            else
                break;      //  No work, go back to backends

            //  If reroutable, send to cloud 20% of the time
            //  Here we'd normally use cloud status information
            //
            if (reroutable && argc > 2 && randof (5) == 0) {
                //  Route to random broker peer
                int peer = randof (argc - 2) + 2;
                zmsg_pushmem (msg, argv [peer], strlen (argv [peer]));
                zmsg_send (&msg, cloudbe);
            }
            else {
                zframe_t *frame = (zframe_t *) zlist_pop (workers);
                zmsg_wrap (msg, frame);
                zmsg_send (&msg, localbe);
                capacity--;
            }
        }
    }
    zctx_destroy (&ctx);
    return EXIT_SUCCESS;
}
