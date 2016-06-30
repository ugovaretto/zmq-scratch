#pragma once
//
// Created by Ugo Varetto on 6/30/16.
//
#include <cinttypes>
#include <vector>

using Byte = std::uint8_t;
using ByteArray = std::vector< Byte >;
using ByteIterator = ByteArray::iterator;
using ConstByteIterator = ByteArray::const_iterator;

//definitions
template < typename T >
struct Serialize {
    static ByteArray Pack(const T &d, ByteArray buf) {
        const size_t sz = buf.size();
        buf.resize(buf.size() + sizeof(d));
        memmove(buf.data() + sz, &d, sizeof(d));
        return buf;
    }
    static ByteIterator Pack(const T &d, ByteIterator i) {
        memmove(i, &d, sizeof(d));
        return i + sizeof(d);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, T &d) {
        memmove(&d, &*i, sizeof(T));
        return i + sizeof(T);
    }
};
//specializations
///@todo specialize for std::vector<T> with different specializations
///for POD and non-POD types: in case of POD simply use memmove, for non-POD
///perform element-wise copy construction w/ placement new
//template < typename T >
//struct Serialize< std::vector< T > > {
//    using V = std::vector< T >;
//    static ByteArray Pack(const V& d, ByteArray buf) {
//        const size_t sz = buf.size();
//        const size_t bytesize = d.size() * sizeof(V::value_type);
//        buf.resize(buf.size() + bytesize);
//        ByteIterator bi = buf.begin() + sz;
//        for(V::iterator i = d.begin(); i != d.end(); ++i) {
//            new(bi) V::value_type(*i);
//            bi += sizeof(V::value_type);
//        }
//        return buf;
//    }
//    static ByteIterator Pack(const T &d, ByteIterator i) {
//        memmove(i, &d, sizeof(d));
//        return i + sizeof(d);
//    }
//    static ConstByteIterator UnPack(ConstByteIterator i, T &d) {
//        memmove(&d, &*i, sizeof(T));
//        return i + sizeof(T);
//    }
//};