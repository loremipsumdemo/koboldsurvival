#pragma once

// The index of an enum class value, for the arrays and loops that count through the values of one
template <class E>
constexpr int idx(E e) { return static_cast<int>(e); }
