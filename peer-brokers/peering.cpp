//  Broker peering simulation (part 3)
//  Prototypes the full flow of status and tasks
#ifdef __APPLE__
#include <ZeroMQ/zmq.h>
#else
#include <zmq.h>
#endif



//  Our own name; in practice, this would be configured per node
static std::string self;

//  .split client task
//  This is the client task. It issues a burst of requests and then
//  sleeps for a few seconds. This simulates sporadic activity; when
//  a number of clients are active at once, the local workers should
//  be overloaded. The client uses a REQ socket for requests and also
//  pushes statistics to the monitor socket:

#ifdef zerorecv
#error "zerocv already defined"
#else
#define zerorecv(s) assert(zmq_recv(s, 0, 0, 0) == 0)
#endif

#ifdef zerosend
#error "zerosend already defined"
#else
#define zerosend(s) assert(zmq_send(s, 0, 0, ZMQ_SNDMORE) == 0)
#endif


//------------------------------------------------------------------------------
class Client {
public:    
    Client(int id, const std::string& text) : 
        id_(id), text_(text) {}
    void operator()() const {
        //initilize context and set REQ identifier to id
        void* context = zmq_ctx_new();
        void* socket = zmq_socket(context, ZMQ_REQ);
        zmq_setsockopt(socket, ZMQ_IDENTITY, &id_, sizeof(id_));
        zmq_connect(socket, FRONTEND_URI);
        std::vector< char > buffer = std::vector< char >(text_.begin(), 
                                                        text_.end());
        buffer.push_back(char(0)); 
        
        int rc = zmq_send(socket, &buffer[0], text_.length(), 0);
        rc = zmq_recv(socket, &buffer[0], buffer.size(), 0);
        buffer[rc] = char(0);
        printf("%d: %s -> %s\n", id_, text_.c_str(), &buffer[0]);  
    }
private:
    int id_;
    std::string text_;
    mutable void* context_;
    mutable void* socket_;
    mutable std::vector< char > buffer_;
};

//------------------------------------------------------------------------------
class Worker {
public:    
    Worker(int id) : 
        id_(id) {}
    void operator()() const {
        void* context = zmq_ctx_new();
        void* socket = zmq_socket(context, ZMQ_REQ);
        std::vector< char > buffer(0x100, char(0));
        zmq_setsockopt(socket, ZMQ_IDENTITY, &id_, sizeof(id_));
        zmq_connect(socket, BACKEND_URI);
        zmq_send(socket, &WORKER_READY, sizeof(WORKER_READY), 0);
        int client_id = -1;
        int rc = -1;
        while(true) {
            zmq_recv(socket, &client_id, sizeof(client_id), 0);
            rc = zmq_recv(socket, 0, 0, 0);
            //assert(rc == 0);
            rc = zmq_recv(socket, &buffer[0], buffer.size(), 0);
            buffer[rc] = char(0);
            const std::string txt(&buffer[0]);
            std::copy(txt.rbegin(), txt.rend(), buffer.begin());
            zmq_send(socket, &client_id, sizeof(client_id), ZMQ_SNDMORE);
            zmq_send(socket, 0, 0, ZMQ_SNDMORE);
            zmq_send(socket, &buffer[0], txt.size(), 0);
        }
        zmq_close(socket);
        zmq_ctx_destroy(context);
    }
private:
    int id_;
  
};


static void *
client_task (void *args)
{
    zctx_t *ctx = zctx_new ();
    void *client = zsocket_new (ctx, ZMQ_REQ);
    zsocket_connect (client, "ipc://%s-localfe.ipc", self);
    void *monitor = zsocket_new (ctx, ZMQ_PUSH);
    zsocket_connect (monitor, "ipc://%s-monitor.ipc", self);

    while (true) {
        sleep (randof (5));
        int burst = randof (15);
        while (burst--) {
            char task_id [5];
            sprintf (task_id, "%04X", randof (0x10000));

            //  Send request with random hex ID
            zstr_send (client, task_id);

            //  Wait max ten seconds for a reply, then complain
            zmq_pollitem_t pollset [1] = { { client, 0, ZMQ_POLLIN, 0 } };
            int rc = zmq_poll (pollset, 1, 10 * 1000 * ZMQ_POLL_MSEC);
            if (rc == -1)
                break;          //  Interrupted

            if (pollset [0].revents & ZMQ_POLLIN) {
                char *reply = zstr_recv (client);
                if (!reply)
                    break;              //  Interrupted
                //  Worker is supposed to answer us with our task id
                assert (streq (reply, task_id));
                zstr_send (monitor, "%s", reply);
                free (reply);
            }
            else {
                zstr_send (monitor,
                    "E: CLIENT EXIT - lost task %s", task_id);
                return NULL;
            }
        }
    }
    zctx_destroy (&ctx);
    return NULL;
}

//  .split worker task
//  This is the worker task, which uses a REQ socket to plug into the
//  load-balancer. It's the same stub worker task that you've seen in 
//  other examples:

static void *
worker_task (void *args)
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

        //  Workers are busy for 0/1 seconds
        sleep (randof (2));
        zmsg_send (&msg, worker);
    }
    zctx_destroy (&ctx);
    return NULL;
}

//  .split main task
//  The main task begins by setting up all its sockets. The local frontend
//  talks to clients, and our local backend talks to workers. The cloud
//  frontend talks to peer brokers as if they were clients, and the cloud
//  backend talks to peer brokers as if they were workers. The state
//  backend publishes regular state messages, and the state frontend
//  subscribes to all state backends to collect these messages. Finally,
//  we use a PULL monitor socket to collect printable messages from tasks:

int main (int argc, char *argv [])
{
    //  First argument is this broker's name
    //  Other arguments are our peers' names
    if (argc < 2) {
        std::cout << "syntax: " << argv[0] << " me {you}...\n";
        return 0;
    }
    const int NBR_CLIENTS = 10;
    const int NBR_WORKERS = 5;
    self = argv [1];
    std::cout << "I: preparing broker at " << self << std::endl;
    
    //  Prepare local frontend and backend
    void* ctx = zmq_ctx_new();
    assert(ctx);
    void *localfe = zmq_socket(ctx, ZMQ_ROUTER);
    assert(localfe);
    assert(zmq_bind(localfe, ("ipc://" + self + "-localfe.ipc").c_str()) == 0);

    void *localbe = zmq_socket(ctx, ZMQ_ROUTER);
    assert(localbe);
    assert(zmq_bind(localbe, ("ipc://" + self + "-localbe.ipc").c_str()) == 0);

    //  Bind cloud frontend to endpoint
    void *cloudfe = zmq_socket(ctx, ZMQ_ROUTER);
    assert(cloudfe);
    //also copy null terminator
    assert(zmq_setsockopt(
            cloudfe, ZMQ_IDENTITY, self.c_str(), self.size() + 1) == 0);
    assert(zmq_bind(cloudfe, ("ipc://" + self + "-cloud.ipc").c_str()) == 0);
    
    //  Connect cloud backend to all peers
    void *cloudbe = zsocket_new (ctx, ZMQ_ROUTER);
    assert(cloudbe);
    zsockopt_set_identity (cloudbe, self);
    int argn;
    for(argn = 2; argn < argc; argn++) {
        const std::string peer = argv[argn];
        std::cout << "I: connecting to cloud frontend at '" 
                  << peer << "'\n";
        assert(zmq_connect(
            cloudbe, ("ipc://" + peer + "-cloud.ipc").c_str()) == 0);
    }

    //  Bind state backend to endpoint
    void *statebe = zmq_socket(ctx, ZMQ_PUB);
    assert(statebe);
    assert(zmq_bind(statebe, ("ipc://" + self + "-state.ipc").c_str()) == 0);

    //  Connect state frontend to all peers
    void *statefe = zmq_socket(ctx, ZMQ_SUB);
    assert(statefe);
    assert(zmq_setsockopt(statefe, ZMQ_SUBSCRIBE, "", 0) == 0);
    for(argn = 2; argn < argc; argn++) {
        const std::string peer = argv [argn];
        std::cout << "I: connecting to state backend at '" 
                  << peer << "'\n";
        assert(zmq_connect(
                statefe, ("ipc://" + peer + "-state.ipc").c_str()) == 0);
    }
    //  Prepare monitor socket
    void *monitor = zmq_socket(ctx, ZMQ_PULL);
    assert(monitor);
    assert(zmq_bind(monitor, ("ipc://" + self + "-monitor.ipc").c_str()) == 0);

    //  .split start child tasks
    //  After binding and connecting all our sockets, we start our child
    //  tasks - workers and clients:

    typedef std::vector< std::thread > Threads;
    Threads workers;
    int worker_nbr = 0;
    for(worker_nbr = 0; worker_nbr < NBR_WORKERS; worker_nbr++)
        workers.push_back(std::thread(Worker(worker_nbr + 1)));

    //  Start local clients
    Threads clients;
    int client_nbr = 0;
    for (client_nbr = 0; client_nbr < NBR_CLIENTS; client_nbr++)
        clients.push_back(std::thread(Client(client_nbr + 1)));

    //  Queue of available workers
    int local_capacity = 0;
    int cloud_capacity = 0;
    typedef int SocketID; 
    std::dequeue< SocketID > worker_queue; 
    enum {LOCAL_BE = 0,
          CLOUD_BE = 1,
          STATE_FE = 2,
          MONITOR  = 3};
    //  .split main loop
    //  The main loop has two parts. First, we poll workers and our two service
    //  sockets (statefe and monitor), in any case. If we have no ready workers,
    //  then there's no point in looking at incoming requests. These can remain 
    //  on their internal 0MQ queues:
    const int WORKER_READY = 0;
    while (true) {
        zmq_pollitem_t primary [] = {
            { localbe, 0, ZMQ_POLLIN, 0 },
            { cloudbe, 0, ZMQ_POLLIN, 0 },
            { statefe, 0, ZMQ_POLLIN, 0 },
            { monitor, 0, ZMQ_POLLIN, 0 }
        };
        //  If we have no workers ready, wait indefinitely
        int rc = zmq_poll (primary, 4,
            worker_queue.size() != 0 ? 1000: -1);
        if (rc == -1)
            break;              //  Interrupted

        //  Track if capacity changes during this iteration
        int previous = int(worker_queue.size());
        std::vector< char > msg(0x100, char(0));
        SocketID workerID;
        SocketID clientID;
        int msgSize = -1;

        if(primary[LOCAL_BE].revents & ZMQ_POLLIN) {
            rc = zmq_recv(localbe, &workerID, sizeof(workerID), 0);
            if(rc == -1)
                break;
            zerorecv(localbe);
            rc = zmq_recv(localbe, &clientID, sizeof(clientID), 0);
            if(rc == -1)
                break;
            worker_queue.push_back(workerID);
            if(clientID != WORKER_READY) {
                zerorecv(localbe);
                rc = zmq_recv(localbe, &msg[0], msg.size(), 0);
                msgSize = rc;
            }
        // Or handle reply from peer broker
        } else if (primary[CLOUD_BE].revents & ZMQ_POLLIN) {
            //cloud id
            rc = zmq_recv(cloudbe, &msg[0], msg.size(), 0);
            if(rc == -1)
                break;
            assert(rc > 0);
            rc = zmq_recv(cloudbe, &clientID, sizeof(clientID), 0); 
            rc = zmq_recv(cloudbe, &msg[0], msg.size(), 0);
            if(rc == -1)
                break;
            msgSize = rc;
        }
        //  Route reply to cloud if it's addressed to a broker
        for (argn = 2; msg && argn < argc; argn++) {
            char *data = (char *) zframe_data (zmsg_first (msg));
            size_t size = zframe_size (zmsg_first (msg));
            if (size == strlen (argv [argn])
            &&  memcmp (data, argv [argn], size) == 0)
                zmsg_send (&msg, cloudfe);
        }
        //  Route reply to client if we still need to
        if (msg)
            zmsg_send (&msg, localfe);

        //  .split handle state messages
        //  If we have input messages on our statefe or monitor sockets, we
        //  can process these immediately:

        if (primary [2].revents & ZMQ_POLLIN) {
            char *peer = zstr_recv (statefe);
            char *status = zstr_recv (statefe);
            cloud_capacity = atoi (status);
            free (peer);
            free (status);
        }
        if (primary [3].revents & ZMQ_POLLIN) {
            char *status = zstr_recv (monitor);
            printf ("%s\n", status);
            free (status);
        }
        //  .split route client requests
        //  Now route as many clients requests as we can handle. If we have
        //  local capacity, we poll both localfe and cloudfe. If we have cloud
        //  capacity only, we poll just localfe. We route any request locally
        //  if we can, else we route to the cloud.

        while (local_capacity + cloud_capacity) {
            zmq_pollitem_t secondary [] = {
                { localfe, 0, ZMQ_POLLIN, 0 },
                { cloudfe, 0, ZMQ_POLLIN, 0 }
            };
            if (local_capacity)
                rc = zmq_poll (secondary, 2, 0);
            else
                rc = zmq_poll (secondary, 1, 0);
            assert (rc >= 0);

            if (secondary [0].revents & ZMQ_POLLIN)
                msg = zmsg_recv (localfe);
            else
            if (secondary [1].revents & ZMQ_POLLIN)
                msg = zmsg_recv (cloudfe);
            else
                break;      //  No work, go back to primary

            if (local_capacity) {
                zframe_t *frame = (zframe_t *) zlist_pop (workers);
                zmsg_wrap (msg, frame);
                zmsg_send (&msg, localbe);
                local_capacity--;
            }
            else {
                //  Route to random broker peer
                int peer = randof (argc - 2) + 2;
                zmsg_pushmem (msg, argv [peer], strlen (argv [peer]));
                zmsg_send (&msg, cloudbe);
            }
        }
        //  .split broadcast capacity
        //  We broadcast capacity messages to other peers; to reduce chatter,
        //  we do this only if our capacity changed.

        if (local_capacity != previous) {
            //  We stick our own identity onto the envelope
            zstr_sendm (statebe, self);
            //  Broadcast new capacity
            zstr_send (statebe, "%d", local_capacity);
        }
    }
    //  When we're done, clean up properly
    while (zlist_size (workers)) {
        zframe_t *frame = (zframe_t *) zlist_pop (workers);
        zframe_destroy (&frame);
    }
    zlist_destroy (&workers);
    zctx_destroy (&ctx);
    return EXIT_SUCCESS;
}
