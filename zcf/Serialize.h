#pragma once
//
// Created by Ugo Varetto on 6/30/16.
//
#include <vector>
#include <string>
#include <type_traits>

#ifdef ZCF_uint8_t
#include <cinttypes>
using Byte = std::uint8_t;
#else
using Byte = char;
static_assert(sizeof(char) == 1, "sizeof(char) != 1");
#endif
using ByteArray = std::vector< Byte >;
using ByteIterator = ByteArray::iterator;
using ConstByteIterator = ByteArray::const_iterator;

template < typename T >
struct GetSerializer;

//Serializer definitions
template < typename T >
struct SerializePOD {
    static ByteArray Pack(const T &d, ByteArray buf = ByteArray()) {
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
    static ByteArray Pack(const T &d, ByteArray buf = ByteArray()) {
        const size_t sz = buf.size();
        buf.resize(buf.size() + sizeof(d));
        new (buf.data() + sz) T(d); //copy constructor
        return buf;
    }
    static ByteIterator Pack(const T &d, ByteIterator i) {
        new (i) T(d); //copy constructor
        return i + sizeof(d);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, T& d) {
        d = *reinterpret_cast< const T* >(&*i); //assignment operator
        return i + sizeof(T);
    }
};


template < typename T >
struct SerializeVectorPOD {
    using ST = typename std::vector< T >::size_type;
    static ByteArray Pack(const std::vector< T >& d,
                          ByteArray buf = ByteArray()) {
        const size_t sz = buf.size();
        const ST s = d.size();
        buf.resize(buf.size() + sizeof(T) * d.size() + sizeof(ST));
        memmove(buf.data() + sz, &s, sizeof(s));
        memmove(buf.data() + sz + sizeof(s), d.data(), sizeof(T) * d.size());
        return buf;
    }
    static ByteIterator Pack(const std::vector< T > &d, ByteIterator i) {
        const ST s = d.size();
        memmove(&*i, &s, sizeof(s));
        memmove(&*i + sizeof(s), d.data(), d.size() * sizeof(T));
        return i + sizeof(T) * d.size() + sizeof(s);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, std::vector< T >& d) {
        ST s = 0;
        memmove(&s, &*i, sizeof(s));
        d.resize(s);
        memmove(d.data(), &*(i + sizeof(s)), s * sizeof(T));
        return i + sizeof(s) + sizeof(T) * s;
    }
};


template < typename T >
struct SerializeVector {
    using ST = typename std::vector<
            typename std::remove_cv< T >::type >::size_type;
    using TS = typename GetSerializer<
            typename std::remove_cv< T >::type >::Type;
    static ByteArray Pack(const std::vector< T >& d,
                          ByteArray buf = ByteArray()) {
        const size_t sz = buf.size();
        const size_t s = d.size();
        buf.resize(buf.size() + sizeof(s));
        memmove(buf.data() + sz, &s, sizeof(s));
        for(decltype(d.cbegin()) i = d.cbegin(); i != d.cend(); ++i) {
            buf = TS::Pack(*i, buf);
        }
        return buf;
    }
    static ByteIterator Pack(const std::vector< T > &d, ByteIterator bi) {
        const ST s = d.size();
        memmove(bi, &s, sizeof(s));
        for(decltype(cbegin(d)) i = cbegin(d); i != cend(d); ++i) {
            bi = TS::Pack(*i, bi);
        }
        return bi;
    }
    static ConstByteIterator UnPack(ConstByteIterator bi, std::vector< T >& d) {
        ST s = 0;
        memmove(&s, &*bi, sizeof(s));
        bi += sizeof(s);
        d.reserve(s);
        for(ST  i = 0; i != s; ++i) {
            T data;
            bi = TS::UnPack(bi, data);
            d.push_back(std::move(data));
        }
        return bi;
    }
};

struct SerializeString {
    using T = std::string::value_type;
    using V = std::vector< T >;
    static ByteArray Pack(const std::string& d,
                          ByteArray buf = ByteArray()) {
        return SerializeVectorPOD< T >::Pack(V(d.begin(), d.end()), buf);
    }
    static ByteIterator Pack(const std::string &d, ByteIterator bi) {
        return SerializeVectorPOD< T >::Pack(V(d.begin(), d.end()), bi);
    }
    static ConstByteIterator UnPack(ConstByteIterator bi, std::string& d) {
        V v;
        bi = SerializeVectorPOD< T >::UnPack(bi, v);
        d = std::string(v.begin(), v.end());
        return bi;
    }
};

// GetSerializer
template < typename T >
struct GetSerializer< std::vector< T > > {
    using NCV = typename std::remove_cv< T >::type;
    using Type = typename std::conditional< std::is_pod< NCV >::value,
            SerializeVectorPOD< NCV >,
            SerializeVector< NCV > >::type;
};

template < typename T >
struct GetSerializer< const std::vector< T > >{
    using Type = typename GetSerializer< std::vector< T > >::Type;
};

template < typename T >
struct GetSerializer< volatile std::vector< T > >{
    using Type = typename GetSerializer< std::vector< T > >::Type;
};

template < typename T >
struct GetSerializer{
    using NCV = typename std::remove_cv< T >::type;
    using Type = typename std::conditional< std::is_pod< NCV >::value,
                                   SerializePOD< NCV >,
                                   Serialize< NCV > >::type;
};

template <>
struct GetSerializer< std::string > {
    using Type = SerializeString;
};

template <>
struct GetSerializer< const std::string > {
    using Type = SerializeString;
};

template <>
struct GetSerializer< volatile std::string > {
    using Type = SerializeString;
};


template < typename H, typename...T >
struct And {
    static const bool Value = H::value && And< T... >::Value;
};

template < typename T >
struct And< T > {
    static const bool Value = T::value;
};

template < typename...ArgsT >
struct GetSerializer< std::tuple< ArgsT... > > {
    using Type =
        typename std::conditional< And< std::is_pod< ArgsT >... >::Value,
                                   SerializePOD< std::tuple< ArgsT... > >,
                                   Serialize< std::tuple< ArgsT... > > >::type;
};

template < typename...ArgsT >
struct GetSerializer< const std::tuple< ArgsT... > > {
    using Type =
    typename std::conditional< And< std::is_pod< ArgsT >... >::Value,
            SerializePOD< std::tuple< ArgsT... > >,
            Serialize< std::tuple< ArgsT... > > >::type;
};

template < typename...ArgsT >
struct GetSerializer< volatile std::tuple< ArgsT... > > {
    using Type =
    typename std::conditional< And< std::is_pod< ArgsT >... >::Value,
            SerializePOD< std::tuple< ArgsT... > >,
            Serialize< std::tuple< ArgsT... > > >::type;
};

//prevent from automatically serialize raw pointers
template < typename T >
struct GetSerializer< T* >;

template < typename T >
struct GetSerializer< const T* >;

template < typename T >
struct GetSerializer< volatile T* >;

//Serializer/DeSerializer adapters to work with RAWI/OStreams
template < typename T >
struct SerializerInstance {
    ByteArray operator()(const T& d, ByteArray ba = ByteArray()) const {
        return GetSerializer< T >::Type::Pack(d, ba);
    }
};

///@todo change after changing RAWInStream deserialization code
///which requires to explicitly pass a size parameter
template < typename T >
struct DeSerializerInstance {
    T operator()(ConstByteIterator bi, size_t /*size*/ = 0) const {
        T d;
        GetSerializer< T >::Type::UnPack(bi, d);
        return d;
    }
};

template < typename T >
ByteArray  Pack(const T& d, ByteArray ba = ByteArray()) {
    return GetSerializer< T >::Type::Pack(d, ba);
}

template < typename T >
typename std::remove_reference<
        typename std::remove_cv< T >::type >::type
UnPack(ConstByteIterator bi) {
    using U = typename std::remove_reference<
                 typename std::remove_cv< T >::type >::type;
    U d;
    GetSerializer< U >::Type::UnPack(bi, d);
    return d;
}

template < typename T >
ConstByteIterator UnPack(ConstByteIterator bi, T& d) {
    return GetSerializer< T >::Type::UnPack(bi, d);
}

template< typename T >
T To(const ByteArray& ba) {
    return UnPack< T >(begin(ba));
}