// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_TRACKER_RCU_H_
#define COMMON_SRC_H_DS_TRACKER_RCU_H_

//#ifdef LOCKFREE_TRACKER

#include <queue>
#include <list>
#include <vector>
#include <atomic>

#include "ds_tracker_concurrent_primitives.h"
// #include "RAllocator.hpp"
#include "ds_tracker_base.h"


enum RCUType {
  type_RCU, type_QSBR
};

template<class T>
class RCUTracker : public BaseTracker<T> {
 private:
  int task_num;
  int freq;
  int epochFreq;
  bool collect;
  RCUType type;

 public:
  class RCUInfo {
   public:
    T *obj;
    uint64_t epoch;

    RCUInfo(T *obj, uint64_t epoch) : obj(obj), epoch(epoch) {}
  };

 private:
  PaddedAtomic<uint64_t> *reservations;
  Padded<uint64_t> *retire_counters;
  Padded<uint64_t> *alloc_counters;
  Padded<std::list<RCUInfo>> *retired;

  std::atomic<uint64_t> epoch;

 public:
  ~RCUTracker() {
    for (int i = 0; i < task_num; i++) {
      while (!retired[i].ui.empty()) {
        auto node = retired[i].ui.front().obj;
        retired[i].ui.pop_front();
        free(node);
      }
    }
//    for (int i = 0; i < task_num; i++) {
//      auto it = retired[i].ui.begin();
//      while (it != retired[i].ui.end()) {
//        free(it->obj);
//      }
//    }
    delete[] retired;
    delete[] reservations;
    delete[] retire_counters;
    delete[] alloc_counters;
  }

  RCUTracker(int task_num, int epochFreq, int emptyFreq, RCUType type, bool collect) :
      BaseTracker<T>(task_num), task_num(task_num), freq(emptyFreq), epochFreq(epochFreq), collect(collect),
      type(type) {
    retired = new Padded<std::list<RCUTracker<T>::RCUInfo>>[task_num];
    reservations = new PaddedAtomic<uint64_t>[task_num];
    retire_counters = new Padded<uint64_t>[task_num];
    alloc_counters = new Padded<uint64_t>[task_num];
    for (int i = 0; i < task_num; i++) {
      reservations[i].ui.store(UINT64_MAX, std::memory_order_release);
      retired[i].ui.clear();
    }
    epoch.store(0, std::memory_order_release);
  }

  RCUTracker(int task_num, int epochFreq, int emptyFreq) : RCUTracker(task_num, epochFreq, emptyFreq, type_RCU, true) {}

  RCUTracker(int task_num, int epochFreq, int emptyFreq, bool collect) :
      RCUTracker(task_num, epochFreq, emptyFreq, type_RCU, collect) {}

  void __attribute__((deprecated)) reserve(uint64_t e, int tid) {
    return start_op(tid);
  }

  void *alloc(int tid) {
    alloc_counters[tid] = alloc_counters[tid] + 1;
    if (alloc_counters[tid] % (epochFreq * task_num) == 0) {
      epoch.fetch_add(1, std::memory_order_acq_rel);
    }
    // return (void *) malloc(sizeof(T));
    return reinterpret_cast<void *>(malloc(sizeof(T)));
  }

  void start_op(int tid) {
    if (type == type_RCU) {
      uint64_t e = epoch.load(std::memory_order_acquire);
      reservations[tid].ui.store(e, std::memory_order_seq_cst);
    }
  }

  void end_op(int tid) {
    if (type == type_RCU) {
      reservations[tid].ui.store(UINT64_MAX, std::memory_order_seq_cst);
    } else {  // if type == TYPE_QSBR
      uint64_t e = epoch.load(std::memory_order_acquire);
      reservations[tid].ui.store(e, std::memory_order_seq_cst);
    }
  }

  void reserve(int tid) {
    start_op(tid);
  }

  void clear(int tid) {
    end_op(tid);
  }


  inline void incrementEpoch() {
    epoch.fetch_add(1, std::memory_order_acq_rel);
  }

  void __attribute__((deprecated)) retire(T *obj, uint64_t e, int tid) {
    return retire(obj, tid);
  }

  void retire(T *obj, int tid) {
    if (obj == NULL) { return; }
    std::list<RCUInfo> *myTrash = &(retired[tid].ui);
    // for(auto it = myTrash->begin(); it!=myTrash->end(); it++){
    // assert(it->obj!=obj && "double retire error");
    // }

    uint64_t e = epoch.load(std::memory_order_acquire);
    RCUInfo info = RCUInfo(obj, e);
    myTrash->push_back(info);
    retire_counters[tid] = retire_counters[tid] + 1;
    if (collect && retire_counters[tid] % freq == 0) {
      empty(tid);
    }
  }

  void empty(int tid) {
    uint64_t minEpoch = UINT64_MAX;
    for (int i = 0; i < task_num; i++) {
      uint64_t res = reservations[i].ui.load(std::memory_order_acquire);
      if (res < minEpoch) {
        minEpoch = res;
      }
    }

    // erase safe objects
    std::list<RCUInfo> *myTrash = &(retired[tid].ui);
    for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;) {
      RCUInfo res = *iterator;
      if (res.epoch < minEpoch) {
        iterator = myTrash->erase(iterator);
        this->reclaim(res.obj);
        this->dec_retired(tid);
      } else { ++iterator; }
    }
  }

  bool collecting() { return collect; }
};

//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_RCU_H_
