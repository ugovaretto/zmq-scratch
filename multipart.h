#pragma once
//Send/receive multipart messages as array of char arrays
//Author: Ugo Varetto
#include <vector>
#include <algorithm>
//------------------------------------------------------------------------------
inline std::vector< std::vector< char > >
recv_messages(void* socket) {
    int rc = -1;
    bool finished = false;
    int opt = 0;
    size_t len = sizeof(opt);
    std::vector< std::vector< char > > ret;
    std::vector< char > buffer(0x100);
    rc = zmq_recv(socket, &buffer[0], buffer.size(), 0);
    if(rc < 0) return ret;
    buffer.resize(rc);
    ret.push_back(buffer);
    buffer.resize(0x100);
    while(true) {
        opt = 0;
        if(zmq_getsockopt(socket, ZMQ_RCVMORE, &opt, &len) != 0) {
			std::cerr << zmq_strerror(errno) << std::endl;
			return ret;
		}
        if(opt) {
        	rc = zmq_recv(socket, &buffer[0], buffer.size(), 0);
        	if(rc < 0) break;
            buffer.resize(rc);
            ret.push_back(buffer);
        } else break;
    }
   return ret;
}
//------------------------------------------------------------------------------
inline void send_messages(void* socket,
              const std::vector< std::vector< char > >& msgs) {
   std::for_each(msgs.begin(), --msgs.end(), 
                [socket](const std::vector< char >& msg){
       const int rc = zmq_send(socket, &msg[0], msg.size(), ZMQ_SNDMORE);
       assert(rc == msg.size());  
   });
   const int rc = zmq_send(socket, &(msgs.back()[0]), msgs.back().size(), 0);
   assert(rc == msgs.back().size());
}