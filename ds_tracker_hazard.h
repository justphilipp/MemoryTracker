// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_TRACKER_HAZARD_H_
#define COMMON_SRC_H_DS_TRACKER_HAZARD_H_

//#ifdef LOCKFREE_TRACKER

#include <queue>
#include <list>
#include <vector>
#include <atomic>

#include "ds_tracker_concurrent_primitives.h"
// #include "RAllocator.hpp"
#include "ds_tracker_base.h"

template<class T>
class HazardTracker : public BaseTracker<T> {
 private:
  int task_num;
  int slotsPerThread;
  int freq;
  bool collect;


  PaddedAtomic<T *> *slots;
  Padded<int> *cntrs;
  // TODO(Bohan Wang): replace vector with map
  Padded<std::list<T *>> *retired;  // TODO(Bohan Wang): use different structure to prevent malloc locking....

  void empty(int tid) {
    std::list<T *> *myTrash = &(retired[tid].ui);
    for (typename std::list<T *>::iterator iterator = myTrash->begin(), end = myTrash->end(); iterator != end;) {
      bool danger = false;
      auto ptr = *iterator;
      for (int i = 0; i < task_num * slotsPerThread; i++) {
        if (ptr == slots[i].ui) {
          danger = true;
          break;
        }
      }
      if (!danger) {
        this->reclaim(ptr);
        this->dec_retired(tid);
        iterator = myTrash->erase(iterator);
      } else { ++iterator; }
    }
  }

 public:
  ~HazardTracker() {
    delete[] slots;
    delete[] cntrs;

    for (int i = 0; i < task_num; i++) {
      while (!retired[i].ui.empty()) {
        T *node = retired[i].ui.front();
        retired[i].ui.pop_front();
        free(node);
      }
    }
    delete[] retired;
  }

  HazardTracker(int task_num, int slotsPerThread, int emptyFreq, bool collect) : BaseTracker<T>(task_num) {
    this->task_num = task_num;
    this->slotsPerThread = slotsPerThread;
    this->freq = emptyFreq;
    slots = new PaddedAtomic<T *>[task_num * slotsPerThread];
    for (int i = 0; i < task_num * slotsPerThread; i++) {
      slots[i] = NULL;
    }
    retired = new Padded<std::list<T *>>[task_num];
    cntrs = new Padded<int>[task_num];
    for (int i = 0; i < task_num; i++) {
      cntrs[i] = 0;
      retired[i].ui = std::list<T *>();
    }
    this->collect = collect;
  }

  HazardTracker(int task_num, int slotsPerThread, int emptyFreq) :
      HazardTracker(task_num, slotsPerThread, emptyFreq, true) {}

  T *read(std::atomic<T *> const &obj, int idx, int tid) {
    T *ret;
    T *realptr;
    while (true) {
      ret = obj.load(std::memory_order_acquire);
      // realptr = (T *) ((size_t) ret & 0xfffffffffffffffc);
      realptr = reinterpret_cast<T *>(reinterpret_cast<size_t>(ret) & 0xfffffffffffffffc);
      reserve(realptr, idx, tid);
      if (ret == obj.load(std::memory_order_acquire)) {
        return ret;
      }
    }
  }

  void reserve(T *ptr, int slot, int tid) {
    slots[tid * slotsPerThread + slot] = ptr;
  }

  void clearSlot(int slot, int tid) {
    slots[tid * slotsPerThread + slot] = NULL;
  }

  void clearAll(int tid) {
    for (int i = 0; i < slotsPerThread; i++) {
      slots[tid * slotsPerThread + i] = NULL;
    }
  }

  // TODO(Bohan Wang) : only HP uses this method, which can be moved to end_op().
  void clear_all(int tid) {
    clearAll(tid);
  }

  void retire(T *ptr, int tid) {
    if (ptr == NULL) { return; }
    std::list<T *> *myTrash = &(retired[tid].ui);
    // for(auto it = myTrash->begin(); it!=myTrash->end(); it++){
    // assert(*it !=ptr && "double retire error");
    // }
    myTrash->push_back(ptr);
    cntrs[tid].ui++;
    if (collect && cntrs[tid] == freq) {
      cntrs[tid] = 0;
      empty(tid);
    }
  }


  bool collecting() { return collect; }


  // for test only
 public:
  int checkmember() {
    return task_num * slotsPerThread;
  }

  bool check_reservation(T *ptr, int slot, int tid) {
    return slots[tid * slotsPerThread + slot] == ptr;
  }
};

//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_HAZARD_H_
