//  Load-balancing broker
//  Clients and workers are shown here in-process

#include <pthread.h>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <ZeroMQ/zmq.h>
#define NBR_CLIENTS 10
#define NBR_WORKERS 3

static const size_t READY = 123;
//  Dequeue operation for queue implemented as array of anything
#define DEQUEUE(q) memmove (&(q)[0], &(q)[1], sizeof (q) - sizeof (q[0]))

static int CIDS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static int WIDS[] = {11, 22, 33};

#define FRONTEND_URI "tcp://0.0.0.0:5555" //ipc://frontend.ipc"
#define BACKEND_URI  "tcp://0.0.0.0:6666"  //"ipc://backend.ipc"


//  Basic request-reply client using REQ socket
//  Because s_send and s_recv can't handle 0MQ binary identities, we
//  set a printable text identity to allow routing.
//
static void *
client_task (void *args)
{
    const int id = *((int*)(args)); 
    void *context = zmq_ctx_new ();
    void *client = zmq_socket (context, ZMQ_REQ);
    zmq_setsockopt(client, ZMQ_IDENTITY, &id, sizeof(id));        //  Set a printable identity
    zmq_connect (client, FRONTEND_URI);

    //  Send request, get reply
    zmq_send(client, "HELLO", strlen("HELLO"), 0);
    char reply[0x100];
    int rc = zmq_recv(client, &reply[0], 0x100, 0);
    reply[rc] = '\0';
    printf ("Client: %s\n", reply);
    zmq_close (client);
    zmq_ctx_destroy (context);
    return NULL;
}

//  .split worker task
//  While this example runs in a single process, that is just to make
//  it easier to start and stop the example. Each thread has its own
//  context and conceptually acts as a separate process.
//  This is the worker task, using a REQ socket to do load-balancing.
//  Because s_send and s_recv can't handle 0MQ binary identities, we
//  set a printable text identity to allow routing.

static void *
worker_task (void *args)
{
    const int id = *((int*)(args)); 
    void *context = zmq_ctx_new ();
    void *worker = zmq_socket (context, ZMQ_REQ);
    zmq_setsockopt(worker, ZMQ_IDENTITY, &id, sizeof(id)); //  Set a printable identity
    zmq_connect (worker, BACKEND_URI);

    //  Tell broker we're ready for work
    zmq_send(worker, &READY, sizeof(READY), 0);
    char request[0x100];
    while (1) {
        //  Read and save all frames until we get an empty frame
        //  In this example there is only 1, but there could be more
        int identity = -1;
        zmq_recv(worker, &identity, sizeof(identity), 0);
        int rc = zmq_recv(worker, 0, 0, 0);
        assert(rc == 0);

        //  Get request, send reply
        rc = zmq_recv(worker, &request[0], 0x100, 0);
        request[rc] = '\0';
        printf ("Worker: %s\n", request);

        zmq_send(worker, &identity, sizeof(identity), ZMQ_SNDMORE);
        zmq_send(worker, 0, 0, ZMQ_SNDMORE);
        zmq_send(worker, "OK", strlen("OK"), 0);
    }
    zmq_close (worker);
    zmq_ctx_destroy (context);
    return NULL;
}

//  .split main task
//  This is the main task. It starts the clients and workers, and then
//  routes requests between the two layers. Workers signal READY when
//  they start; after that we treat them as ready when they reply with
//  a response back to a client. The load-balancing data structure is 
//  just a queue of next available workers.

int main (void)
{
    //  Prepare our context and sockets
    void *context = zmq_ctx_new ();
    void *frontend = zmq_socket (context, ZMQ_ROUTER);
    void *backend  = zmq_socket (context, ZMQ_ROUTER);
    zmq_bind (frontend, FRONTEND_URI);
    zmq_bind (backend,  BACKEND_URI);

    int client_nbr;
    for (client_nbr = 0; client_nbr < NBR_CLIENTS; client_nbr++) {
        pthread_t client;
        pthread_create (&client, NULL, client_task, &CIDS[client_nbr]);
    }
    int worker_nbr;
    for (worker_nbr = 0; worker_nbr < NBR_WORKERS; worker_nbr++) {
        pthread_t worker;
        pthread_create (&worker, NULL, worker_task, &WIDS[worker_nbr]);
    }
    //  .split main task body
    //  Here is the main loop for the least-recently-used queue. It has two
    //  sockets; a frontend for clients and a backend for workers. It polls
    //  the backend in all cases, and polls the frontend only when there are
    //  one or more workers ready. This is a neat way to use 0MQ's own queues
    //  to hold messages we're not ready to process yet. When we get a client
    //  reply, we pop the next available worker and send the request to it,
    //  including the originating client identity. When a worker replies, we
    //  requeue that worker and forward the reply to the original client
    //  using the reply envelope.

    //  Queue of available workers
    int available_workers = 0;
    int worker_queue [10];

    while (1) {
        zmq_pollitem_t items [] = {
            { backend,  0, ZMQ_POLLIN, 0 },
            { frontend, 0, ZMQ_POLLIN, 0 }
        };
        //  Poll frontend only if we have available workers
        int rc = zmq_poll (items, available_workers ? 2 : 1, -1);
        if (rc == -1)
            break;              //  Interrupted

        //  Handle worker activity on backend
        if (items [0].revents & ZMQ_POLLIN) {
            //  Queue worker identity for load-balancing
            int worker_id = -1;
            zmq_recv(backend, &worker_id, sizeof(worker_id), 0);
            assert (available_workers < NBR_WORKERS);
            worker_queue[available_workers++] = worker_id;

            //  Second frame is empty
            int rc = zmq_recv (backend, 0, 0, 0);
            assert (rc == 0);

            //  Third frame is READY or else a client reply identity
            int client_id = -1;
            zmq_recv(backend, &client_id, sizeof(client_id), 0);

            //  If client reply, send rest back to frontend
            if (client_id != READY) {
                rc = zmq_recv(backend, 0, 0, 0);
                assert (rc == 0);
                char reply[0x100];
                rc = zmq_recv(backend, &reply[0], 0x100, 0);
                zmq_send(frontend, &client_id, sizeof(client_id), ZMQ_SNDMORE);
                zmq_send(frontend, 0, 0, ZMQ_SNDMORE);
                zmq_send(frontend, &reply[0], rc, 0);
                if (--client_nbr == 0)
                    break;      //  Exit after N messages
            }
        }
        //  .split handling a client request
        //  Here is how we handle a client request:

        if (items [1].revents & ZMQ_POLLIN) {
            //  Now get next client request, route to last-used worker
            //  Client request is [identity][empty][request]
            int client_id = -1;
            zmq_recv(frontend, &client_id, sizeof(client_id), 0);
            int rc = zmq_recv(frontend, 0, 0, 0);
            assert (rc == 0);
            char request[0x100];
            rc = zmq_recv(frontend, &request[0], 0x100, 0);
            zmq_send(backend, &worker_queue[0], sizeof(int), ZMQ_SNDMORE);
            zmq_send(backend, 0, 0, ZMQ_SNDMORE);
            zmq_send(backend, &client_id, sizeof(client_id), ZMQ_SNDMORE);
            zmq_send(backend, 0, 0, ZMQ_SNDMORE);
            zmq_send(backend, &request[0], rc, 0);

            //  Dequeue and drop the next worker identity
            DEQUEUE (worker_queue);
            available_workers--;
        }
    }
    zmq_close (frontend);
    zmq_close (backend);
    zmq_ctx_destroy (context);
    return 0;
}
