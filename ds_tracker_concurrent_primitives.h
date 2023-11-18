// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_TRACKER_CONCURRENT_PRIMITIVES_H_
#define COMMON_SRC_H_DS_TRACKER_CONCURRENT_PRIMITIVES_H_

//#ifdef LOCKFREE_TRACKER

#include <cassert>
#include <cstddef>
#include <iostream>
#include <atomic>
#include <string>

#ifndef LEVEL1_DCACHE_LINESIZE
#define LEVEL1_DCACHE_LINESIZE 128
#endif

#define CACHE_LINE_SIZE LEVEL1_DCACHE_LINESIZE

// Possibly helpful concurrent data structure primitives

// Pads data to cacheline size to eliminate false sharing



template<typename T>
class Padded {
 public:
  // [[ align(CACHE_LINE_SIZE) ]] T ui;
  T ui;

 private:
  /*uint8_t pad[ CACHE_LINE_SIZE > sizeof(T)
       ? CACHE_LINE_SIZE - sizeof(T)
       : 1 ];*/
  uint8_t pad[0 != sizeof(T) % CACHE_LINE_SIZE
              ? CACHE_LINE_SIZE - (sizeof(T) % CACHE_LINE_SIZE)
              : CACHE_LINE_SIZE];

 public:
  Padded<T>() : ui() {}

  // conversion from T (constructor):
  Padded<T>(const T &val) : ui(val) {}

  // conversion from A (assignment):
  Padded<T> &operator=(const T &val) {
    ui = val;
    return *this;
  }

  // conversion to A (type-cast operator)
  operator T() { return T(ui); }
};  // __attribute__(( aligned(CACHE_LINE_SIZE) )); // alignment confuses valgrind by shifting bits


template<typename T>
class PaddedAtomic {
 public:
  // [[ align(CACHE_LINE_SIZE) ]] T ui;
  std::atomic<T> ui;
 private:
  uint8_t pad[0 != sizeof(T) % CACHE_LINE_SIZE
              ? CACHE_LINE_SIZE - (sizeof(T) % CACHE_LINE_SIZE)
              : CACHE_LINE_SIZE];
 public:
  PaddedAtomic<T>() : ui() {}

  // conversion from T (constructor):
  PaddedAtomic<T>(const T &val) : ui(val) {}

  // conversion from A (assignment):
  PaddedAtomic<T> &operator=(const T &val) {
    ui.store(val);
    return *this;
  }

  // conversion to A (type-cast operator)
  operator T() { return T(ui.load()); }
};  // __attribute__(( aligned(CACHE_LINE_SIZE) )); // alignment confuses valgrind by shifting bits



template<typename T>
class VolatilePadded {
 public:
  //[[ align(CACHE_LINE_SIZE) ]] volatile T ui;
  volatile T ui;
 private:
  uint8_t pad[CACHE_LINE_SIZE > sizeof(T)
              ? CACHE_LINE_SIZE - sizeof(T)
              : 1];
 public:
  VolatilePadded<T>() : ui() {}

  // conversion from T (constructor):
  VolatilePadded<T>(const T &val) : ui(val) {}

  // conversion from T (assignment):
  VolatilePadded<T> &operator=(const T &val) {
    ui = val;
    return *this;
  }

  // conversion to T (type-cast operator)
  operator T() { return T(ui); }
}__attribute__((aligned(CACHE_LINE_SIZE)));


// Counted pointer, used to eliminate ABA problem
template<class T>
class Cptr;

// Counted pointer, local copy.  Non atomic, for use
// to create values for counted pointers.
template<class T>
class CptrLocal {
  uint64_t ui
      __attribute__((aligned(8))) = 0;

 public:
  void init(const T *ptr, const uint32_t sn) {
    uint64_t a;
    a = 0;
    a = (uint32_t) ptr;
    a = a << 32;
    a += sn;
    ui = a;
  }

  void init(const uint64_t initer) {
    ui = initer;
  }

  void init(const Cptr<T> ptr) {
    ui = ptr.all();
  }

  void init(const CptrLocal<T> ptr) {
    ui = ptr.all();
  }

  uint64_t all() const {
    return ui;
  }

  T operator*() { return *this->ptr(); }

  T *operator->() { return this->ptr(); }

  // conversion from T (constructor):
  CptrLocal<T>(const T **val) { init(&val, 0); }

  // conversion to T (type-cast operator)
  operator T *() { return this->ptr(); }

  void storeNull() {
    ui = 0;
  }


  T *ptr() {
    // return (T *) ((ui & 0xffffffff00000000) >> 32);
    return reinterpret_cast<T*>((ui & 0xffffffff00000000) >> 32);
  }

  uint32_t sn() { return (ui & 0x00000000ffffffff); }

  CptrLocal<T>() {
    init(NULL, 0);
  }

  CptrLocal<T>(const uint64_t initer) {
    init(initer);
  }

  CptrLocal<T>(const T *ptr, const uint32_t sn) {
    init(ptr, sn);
  }

  CptrLocal<T>(const CptrLocal<T> &cp) {
    init(cp.all());
  }

  CptrLocal<T>(const Cptr<T> &cp) { init(cp.all()); }

  // conversion from A (assignment):
  CptrLocal<T> &operator=(const Cptr<T> &cp) {
    init(cp.all());
    return *this;
  }
};

// Counted pointer
template<class T>
class Cptr {
  std::atomic<uint64_t> ui
      __attribute__((aligned(8)));

 public:
  void init(const T *ptr, const uint32_t sn) {
    uint64_t a;
    a = 0;
    a = (uint32_t) ptr;
    a = a << 32;
    a += sn;
    ui.store(a, std::memory_order::memory_order_release);
  }

  void init(const uint64_t initer) {
    ui.store(initer);
  }

  T operator*() { return *this->ptr(); }

  T *operator->() { return this->ptr(); }

  // conversion from T (constructor):
  Cptr<T>(const T * const &val) { init(val, 0); }

  // conversion to T (type-cast operator)
  operator T *() { return this->ptr(); }

  T *ptr() {
    // return (T *) (((ui.load(std::memory_order::memory_order_consume)) & 0xffffffff00000000) >> 32);
    return reinterpret_cast<T*>(((ui.load(std::memory_order::memory_order_consume)) & 0xffffffff00000000) >> 32);
  }

  uint32_t sn() { return ((ui.load(std::memory_order::memory_order_consume)) & 0x00000000ffffffff); }

  uint64_t all() const {
    return ui;
  }

  bool CAS(CptrLocal<T> const &oldval, T *newval) {
    CptrLocal<T> replacement;
    replacement.init(newval, oldval.sn() + 1);
    uint64_t old = oldval.all();
    return ui.compare_exchange_strong(old, replacement.all(), std::memory_order::memory_order_release);
  }

  bool CAS(CptrLocal<T> const &oldval, CptrLocal<T> const &newval) {
    CptrLocal<T> replacement;
    replacement.init(newval.ptr(), oldval.sn() + 1);
    uint64_t old = oldval.all();
    return ui.compare_exchange_strong(old, replacement.all(), std::memory_order::memory_order_release);
  }

  bool CAS(Cptr<T> const &oldval, T *newval) {
    CptrLocal<T> replacement;
    replacement.init(newval, oldval.sn() + 1);
    uint64_t old = oldval.all();
    return ui.compare_exchange_strong(old, replacement.all(), std::memory_order::memory_order_release);
  }

  bool CAS(Cptr<T> const &oldval, CptrLocal<T> const &newval) {
    CptrLocal<T> replacement;
    replacement.init(newval.ptr(), oldval.sn() + 1);
    uint64_t old = oldval.all();
    return ui.compare_exchange_strong(old, replacement.all(), std::memory_order::memory_order_release);
  }

  bool CAS(CptrLocal<T> const &oldval, T *newval, uint32_t newSn) {
    CptrLocal<T> replacement;
    replacement.init(newval, newSn);
    uint64_t old = oldval.all();
    return ui.compare_exchange_strong(old, replacement.all(), std::memory_order::memory_order_release);
  }

  void storeNull() {
    init(NULL, 0);
  }

  void storePtr(T *newval) {
    CptrLocal<T> oldval;
    while (true) {
      oldval.init(all());
      if (CAS(oldval, newval)) { break; }
    }
  }

  Cptr<T>() {
    init(NULL, 0);
  }

  Cptr<T>(const Cptr<T> &cp) {
    init(cp.all());
  }

  Cptr<T>(const CptrLocal<T> &cp) {
    init(cp.all());
  }

  Cptr<T>(const uint64_t initer) {
    init(initer);
  }

  Cptr<T>(const T *ptr, const uint32_t sn) {
    init(ptr, sn);
  }

  /*bool operator==(Cptr<T> &other){
      return other.ui==this->ui;
  }*/
};

//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_CONCURRENT_PRIMITIVES_H_
