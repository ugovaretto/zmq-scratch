//
// Created by Ugo Varetto on 5/10/16.
//

#ifndef ZMQ_SCRATCH_UTILITY_H
#define ZMQ_SCRATCH_UTILITY_H

template < typename T >
const void* Data(const T& v) { return &v; }

template <>
const void* Data< std::string >(const std::string& s) { return s.c_str(); }


template < typename T >
size_t Size(const T& v) { return sizeof(v); }

template <>
size_t Size< std::string >(const std::string& s) { return s.size(); }


void ZCheck(int ret) {
    if(ret < 0) throw std::runtime_error(strerror(errno));
}

template < typename T >
void Send(void *s, T&& d) {
    ZCheck(zmq_send(s, Data(d), Size(d), 0));
}

template < typename T, typename... ArgsT >
void Send(void *s, T&& d, ArgsT&&...args) {
    ZCheck(zmq_send(s, Data(d), Size(d), ZMQ_SNDMORE));
    ZCheck(zmq_send(s, 0, 0, ZMQ_SNDMORE));
    return Send(s, args...);
}
#endif //ZMQ_SCRATCH_UTILITY_H
