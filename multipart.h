#pragma once
//Send/receive multipart messages as array of char arrays
//Author: Ugo Varetto
#include <vector>
#include <algorithm>
typedef std::vector< std::vector< char > > CharArrays;
//------------------------------------------------------------------------------
inline CharArrays
recv_messages(void* socket) {
    int rc = -1;
    bool finished = false;
    int opt = 0;
    size_t len = sizeof(opt);
    CharArrays ret;
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
              const CharArrays& msgs) {
   std::for_each(msgs.begin(), --msgs.end(), 
                [socket](const std::vector< char >& msg){
       const int rc = zmq_send(socket, &msg[0], msg.size(), ZMQ_SNDMORE);
       assert(rc == msg.size());  
   });
   const int rc = zmq_send(socket, &(msgs.back()[0]), msgs.back().size(), 0);
   assert(rc == msgs.back().size());
}
//------------------------------------------------------------------------------
std::string chars_to_string(const std::vector< char >& buf) {
    return std::string(&(*buf.begin()), &(*buf.end()));
}
//------------------------------------------------------------------------------
void push_front(CharArrays& ca, const std::vector< char >& v) {
    ca.insert(ca.begin(), v);
}



