//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2019 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#ifndef STACK_HXX
#define STACK_HXX

#include <array>
#include <functional>

#include "bspf.hxx"

/**
 * Simple fixed size stack class.
 */
namespace Common {

template <class T, uInt32 CAPACITY = 50>
class FixedStack
{
  private:
    array<T, CAPACITY> _stack;
    uInt32 _size;

  public:
    using StackFunction = std::function<void(T&)>;

    FixedStack<T, CAPACITY>() : _size(0) { }

    bool empty() const { return _size <= 0; }
    bool full() const  { return _size >= CAPACITY; }

    T top() const { return _stack[_size - 1];    }
    T get(uInt32 pos) { return _stack[pos]; }
    void push(const T& x) { _stack[_size++] = x; }
    T pop() { return std::move(_stack[--_size]); }
    uInt32 size() const { return _size; }

    // Reverse the contents of the stack
    // This operation isn't needed very often, but it's handy to have
    void reverse() {
      for(uInt32 i = 0, j = _size - 1; i < j; ++i, --j)
        std::swap(_stack[i], _stack[j]);
    }

    // Apply the given function to every item in the stack
    // We do it this way so the stack API can be preserved,
    // and no access to individual elements is allowed outside
    // the class.
    void applyAll(const StackFunction& func) {
      for(uInt32 i = 0; i < _size; ++i)
        func(_stack[i]);
    }

    friend ostream& operator<<(ostream& os, const FixedStack<T>& s) {
      for(uInt32 pos = 0; pos < s._size; ++pos)
        os << s._stack[pos] << " ";
      return os;
    }

  private:
    // Following constructors and assignment operators not supported
    FixedStack(const FixedStack&) = delete;
    FixedStack(FixedStack&&) = delete;
    FixedStack& operator=(const FixedStack&) = delete;
    FixedStack& operator=(FixedStack&&) = delete;
};

}  // Namespace Common

#endif
