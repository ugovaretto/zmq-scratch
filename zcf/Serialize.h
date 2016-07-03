#pragma once
//
// Created by Ugo Varetto on 6/30/16.
//
#include <cinttypes>
#include <vector>
#include <type_traits>

using Byte = std::uint8_t;
using ByteArray = std::vector< Byte >;
using ByteIterator = ByteArray::iterator;
using ConstByteIterator = ByteArray::const_iterator;

//definitions
template < typename T >
struct SerializePOD {
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
    static ConstByteIterator UnPack(ConstByteIterator i, T& d) {
        memmove(&d, &*i, sizeof(T));
        return i + sizeof(T);
    }
};

template < typename T >
struct SerializeVectorPOD {
    static ByteArray Pack(const std::vector< T >& d, ByteArray buf) {
        const size_t sz = buf.size();
        using ST = std::vector< T >::size_type;
        const ST s = d.size():
        buf.resize(buf.size() + sizeof(T) * d.size() + sizeof(ST));
        memmove(buf.data + sz, &s, sizeof(s));
        memmove(buf.data() + sz + sizeof(SZ), &d, sizeof(T) * d.size());
        return buf;
    }
    static ByteIterator Pack(const std::vector< T > &d, ByteIterator i) {
        using ST = std::vector< T >::size_type;
        const ST s = d.size():
        memmove(i, &s, sizeof(s));
        memmove(i + sizeof(s), d.data(), d.size() * sizeof(T));
        return i + sizeof(T) * d.size() + sizeof(s);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, std::vector< T >& d) {
        using ST = std::vector< T >::size_type;
        ST s = 0;
        memmove(&s, &*i, sizeof(s));
        d.resize(s);
        memmove(d.data(), &*(i + sizeof(s)), s * sizeof(T));
        return i + sizeof(s) + sizeof(T) * s;
    }
};

template < typename T >
struct SerializeVector {
    static ByteArray Pack(const std::vector< T >& d, ByteArray buf) {
        const size_t sz = buf.size();
        using ST = std::vector< T >::size_type;
        const ST s = d.size():
        buf.resize(buf.size() + sizeof(T) * d.size() + sizeof(ST));
        using TS = GetSerializer< T >::Type;
        for(:) {
            buf = TS::Pack(*i, b);
        }
        memmove(buf.data + sz, &s, sizeof(s));
        memmove(buf.data() + sz + sizeof(SZ), &d, sizeof(T) * d.size());
        return buf;
    }
    static ByteIterator Pack(const std::vector< T > &d, ByteIterator i) {
        using ST = std::vector< T >::size_type;
        const ST s = d.size():
        memmove(i, &s, sizeof(s));
        memmove(i + sizeof(s), d.data(), d.size() * sizeof(T));
        return i + sizeof(T) * d.size() + sizeof(s);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, std::vector< T >& d) {
        using ST = std::vector< T >::size_type;
        ST s = 0;
        memmove(&s, &*i, sizeof(s));
        d.resize(s);
        memmove(d.data(), &*(i + sizeof(s)), s * sizeof(T));
        return i + sizeof(s) + sizeof(T) * s;
    }
};


template < typename T >
struct InvalidSerializer;

template < typename T >
struct GetSerializer{
    using Type = std::conditional< std::is_pod< T >::value,
                                   SerializerPOD< T >,
                                   InvalidSerializer< T > >::type;
};

template < typename T >
struct GetSerializer< std::vector< T > > {
    using Type = std::conditional< std::is_pod< T >::value,
                                   SerializerVectotPOD< T >,
                                   InvalidSerializer< T > >::type;
};

template < typename...ArgsT >
struct GetSerializer< std::tuple< ArgsT... > > {

};

template <>
struct GetSerializer< std::string > {

};
