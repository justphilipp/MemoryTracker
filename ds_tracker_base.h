// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_TRACKER_BASE_H_
#define COMMON_SRC_H_DS_TRACKER_BASE_H_

//#ifdef LOCKFREE_TRACKER

#include <queue>
#include <list>
#include <vector>
#include <atomic>

#include "ds_tracker_concurrent_primitives.h"

// #include "RAllocator.hpp"


template<class T>
class BaseTracker {
 public:
  Padded<uint64_t> *retired_cnt;

  explicit BaseTracker(int task_num) {
    retired_cnt = new Padded<uint64_t>[task_num];
    for (int i = 0; i < task_num; i++) {
      retired_cnt[i].ui = 0;
    }
  }

  virtual ~BaseTracker() {
    delete[] retired_cnt;
  }

  uint64_t get_retired_cnt(int tid) {
    return retired_cnt[tid].ui;
  }

  void inc_retired(int tid) {
    retired_cnt[tid].ui++;
  }

  void dec_retired(int tid) {
    retired_cnt[tid].ui--;
  }

  virtual void *alloc(int tid) {
    return alloc();
  }

  virtual void *alloc() {
    // return (void *) malloc(sizeof(T));
    return reinterpret_cast<void *>(malloc(sizeof(T)));
  }

  // NOTE: reclaim shall be only used to thread-local objects.
  virtual void reclaim(T *obj) {
    assert(obj != NULL);
    obj->~T();
    free(obj);
  }

  // NOTE: reclaim (obj, tid) should be used on all retired objects.
  virtual void reclaim(T *obj, int tid) {
    reclaim(obj);
  }

  virtual void start_op(int tid) {}

  virtual void end_op(int tid) {}

  virtual T *read(std::atomic<T *> const &obj, int idx, int tid) {
    return obj.load(std::memory_order_acquire);
  }

  virtual void transfer(int src_idx, int dst_idx, int tid) {}

  virtual void reserve(T *obj, int idx, int tid) {}

  virtual void release(int idx, int tid) {}

  virtual void clear_all(int tid) {}

  virtual void retire(T *obj, int tid) {}


  // for TrackerOA
 public:
  virtual bool checkThreadWarning(int tid) {
    return false;
  }

  virtual void resetThreadWarning(int tid) {}

  virtual void oa_read(std::atomic<T *> const &obj, int idx, int tid) {}

  virtual void oa_clear(int tid) {}


// for test only
 public:
  virtual int checkmember() { return 0; }

  virtual int check_retired_count(int tid) { return 0; }

  virtual bool check_reservation(T *ptr, int slot, int tid) { return false; }
};

//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_BASE_H_
