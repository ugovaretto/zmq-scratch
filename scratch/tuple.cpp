//
// Created by Ugo Varetto on 6/30/16.
//
#include <iostream>
#include <tuple>
#include <cstdlib>

using namespace std;

template < int... > struct IndexSequence {};
template < int M, int... Ints >
struct MakeIndexSequence : MakeIndexSequence< M - 1, M - 1, Ints...> {};

template < int... Ints >
struct MakeIndexSequence< 0, Ints... >  { using Type = IndexSequence< Ints... >; };

int foo(int i1, int i2) { return i1 + i2; }

template < int...s >
void printfoo(tuple< int, int > t, IndexSequence< s... > ) {
    cout << foo(get<s>(t)...) << endl;
}


template < typename R, int...Ints, typename...ArgsT >
R Call(std::function< R (ArgsT...) > f,
       std::tuple< ArgsT... > args, const IndexSequence< Ints... >& ) {
    return f(std::get< Ints >(args)...);
};



//template < int i, int Size >
//struct MakeIndexSequence : {
//    using type = MakeIndexSequence< i - 1, size >::type;
//};
//
//
//namespace detail {
//    template <class F, class Tuple, std::size_t... I>
//    constexpr decltype(auto) apply_impl( F&& f, Tuple&& t, std::index_sequence<I...> )
//    {
//        return std::invoke(std::forward<F>(f), std::get<I>(std::forward<Tuple>(t))...);
//        // Note: std::invoke is a C++17 feature
//    }
//} // namespace detail
//
//template <class F, class Tuple>
//constexpr decltype(auto) apply(F&& f, Tuple&& t)
//{
//    return detail::apply_impl(std::forward<F>(f), std::forward<Tuple>(t),
//                              std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>{}>{});
//}

struct C {
    template < typename T = int >
    T operator()(int i) const {
        return T(i);
    }
};


int main(int, char**) {

    auto i12 = make_tuple(1, 3);
    printfoo(i12,
             MakeIndexSequence< tuple_size< decltype(i12) >::value >::Type());
    cout << Call(std::function< int (int) >(C()), make_tuple(19), MakeIndexSequence< 1 >::Type()) << endl << endl;
    C c;
    double d = c(3);
    return EXIT_SUCCESS;
}
