#pragma once
//
// Created by Ugo Varetto on 6/30/16.
//
#include <cinttypes>
#include <vector>
#include <string>
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
struct Serialize {
    static ByteArray Pack(const T &d, ByteArray buf) {
        const size_t sz = buf.size();
        buf.resize(buf.size() + sizeof(d));
        new (buf.data() + size) T(d); //copy constructor
        return buf;
    }
    static ByteIterator Pack(const T &d, ByteIterator i) {
        new (i) T(d); //copy constructor
        return i + sizeof(d);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, T& d) {
        d = *reiterpret_cast< const T* >(i); //assignment operator
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
        const ST s = d.size();
        buf.resize(buf.size() + sizeof(T) * d.size() + sizeof(ST));
        memmove(buf.data + sz, &s, sizeof(s));
        using TS = GetSerializer< T >::Type;
        for(decltype(cbegin(d)) i = cbegin(d); i != cend(d); ++i) {
            buf = TS::Pack(*i, buf);
        }
        return buf;
    }
    static ByteIterator Pack(const std::vector< T > &d, ByteIterator bi) {
        using ST = std::vector< T >::size_type;
        const ST s = d.size();
        memmove(bi, &s, sizeof(s));
        using TS = GetSerializer< T >::Type;
        for(decltype(cbegin(d)) i = cbegin(d); i != cend(d); ++i) {
            bi = TS::Pack(*i, buf);
        }
        return bi;
    }
    static ConstByteIterator UnPack(ConstByteIterator bi, std::vector< T >& d) {
        using ST = std::vector< T >::size_type;
        ST s = 0;
        memmove(&s, &*i, sizeof(s));
        d.reserve(s);
        using TS = GetSerializer< T >::Type;
        for(ST  i = 0; i != s; ++i) {
            T data;
            bi = TS::UnPack(bi, data);
            d.push_back(std::move(data));
        }
        return bi;
    }
};

struct SerializeString {
    using V = std::vector< std::string::value_type >;
    static ByteArray Pack(const std::string& d, ByteArray buf) {
        return SerializeVectorPOD::Pack(V(d.begin(), d.end()), buf);
    }
    static ByteIterator Pack(const std::string &d, ByteIterator bi) {
        return SerializeVectorPOD::Pack(V(d.begin(), d.end()), bi);
    }
    static ConstByteIterator UnPack(ConstByteIterator bi, std::string& d) {
        V v;
        const bi = SerializeVectorPOD::UnPack(bi, v);
        d = std::string(v.begin(), v.end());
        return bi;
    }
};


template < typename T >
struct InvalidSerializer;

template < typename T >
struct GetSerializer{
    using Type = std::conditional< std::is_pod< T >::value,
                                   SerializePOD< T >,
                                   Serialize< T > >::type;
};

template < typename T >
struct GetSerializer< std::vector< T > > {
    using Type = std::conditional< std::is_pod< T >::value,
                                   SerializeVectorPOD< T >,
                                   SerializeVector< T > >::type;
};

template <>
struct GetSerializer< std::string > {
    using Type = SerializeString;
};

//template < typename...ArgsT >
//struct GetSerializer< std::tuple< ArgsT... > > {
//
//};
