#pragma once
//
// Created by Ugo Varetto on 6/30/16.
//

//using IndexType = int;
//Invoker< IndexType > invoker;
//enum {DIV, INV};
//invoker[DIV] = [](float f1, float f2) { return f1 / f2; }
//invoker[INV] = [](float f) { return 1.0f/f; }
//assert(invoker[DIV](1.0f, 3.0f) == 1.0f/3.0f);
//assert(invoker[INV](5.0f) == 1.0f/5.0f;
//std::vector< char > buffer;
//Pack(3.0f, buffer);
//Pack(5.0f, buffer);
//using std::vector< char >::const_iterator = I;
//float f1 = 0.f;
//float f2 = 0.f;
//I i = UnPack(buffer.begin(), f1);
//i = UnPack(i, f2);
//assert(f1 == 3.0f);
//assert(f2 == 5.0f);
//assert(invoker[DIV](buffer) == 1.0f/3.0f);


#include <cinttypes>
#include <vector>
#include <memory>

using Byte = std::uint8_t;
using ByteArray = std::vector< Byte >;
using ByteIterator = ByteArray::iterator;
using ConstByteIterator = ByteArray::const_iterator;

template < typename T, typename...ArgsT >
ByteArray Serialize(ByteArray buf, const T& h, ArgsT...t) {
    ByteArray b = Serializer< T >::Pack(h, buf);
    return Serialize(std::move(b), t...);
};

template<>
ByteArray Serialize(ByteArray buf) {
    return buf;
}


template< int I, int S, typename...ArgsT >
struct DeSerializeHelper {
    static void Do(ConstByteIterator i, std::tuple< ArgsT... >& t) {
        DeSerializeHelper< I + 1, S, ArgsT... >::Do(UnPack(i, get< I >(t)));
    }
};

template< int I, typename...ArgsT >
struct DeSerializerHelper< I, I, ArgsT... > {
    static void Do(ConstByteIterator, std::tuple< ArgsT... >& ) {}
};

template < typename...ArgsT >
void DeSerialize(ConstByteIterator i, std::tuple< ArgsT... >& t) {
    DeSerializeHelper< 0, sizeof...(ArgsT) >::Do(i, t);
}


template < int... > struct IndexSequence {};
template < int M, int... Ints >
struct MakeIndexSequence : MakeIndexSequence< M - 1, M - 1, Ints...> {};

template < int... Ints >
struct MakeIndexSequence< 0, Ints... >  {
    using Type = IndexSequence< Ints... >;
};

struct IInvoke {
    template < typename T, typename... ArgsT >
    ByteArray operator()(ArgsT...params) {
        return Invoke(Serialize(ByteArray(), params));
    }
    virtual ByteArray Invoke(const ByteArray& params) = 0;
    virtual IInvoke* Clone() const = 0;
    virtual ~IInvoke() {}
};

template < typename T >
struct InvokeImpl;

template < typename R, typename...ArgsT >
struct InvokeImpl< std::function< R (ArgsT...) > > {
    using FType = std::function< R (ArgsT...);
    InvokeImpl(const FType& f) : f_(f) {}
    ByteArray Invoke(const ByteArray& params) {
        std::tuple< ArgsT... > params;
        DeSerialize(params.cbegin(), params);
        const R r =
                Call(params, MakeIndexSequence< sizeof...(ArgsT) >::Type());
        return Serialize(ByteArray(), r);
    }
    template < int...s >
    R Call(std::tuple< ArgsT... >& params, IndexSequence< s... >) {
        return f_(get< s >(params)...);
    }
    InvokeImpl* Clone() const {
        return new InvokeImpl(f_);
    }
    FType f_;
};

template < typename...ArgsT >
struct InvokeImpl< std::function< void (ArgsT...) > > {
    using FType = std::function< void (ArgsT...);
    InvokeImpl(const FType& f) : f_(f) {}
    ByteArray Invoke(const ByteArray& params) {
        std::tuple< ArgsT... > params;
        DeSerialize(params.cbegin(), params);
        Call(params, MakeIndexSequence< sizeof...(ArgsT) >::Type());
        return ByteArray();
    }
    InvokeImpl* Clone() const {
        return new InvokeImpl(f_);
    }
    template < int...s >
    void Call(std::tuple< ArgsT... >& params, IndexSequence< s... >) {
        f_(get< s >(params)...);
    }
    FType f_;
};

class Invoker {
    template< typename I >
    Invoker(const I& i) : invoke_(new InvokeImpl< I >(i)) {}
    Invoker(Invoker&&) = default;
    Invoker(const Invoker& i) : invoke_(invoke_->Clone()) {}
    Invoker() = delete;
    std::unique_ptr< IInvoke > invoke_;
};
