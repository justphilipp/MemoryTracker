// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_TRACKER_RANGE_NEW_H_
#define COMMON_SRC_H_DS_TRACKER_RANGE_NEW_H_

//#ifdef LOCKFREE_TRACKER

#include <queue>
#include <list>
#include <vector>
#include <atomic>

#include "ds_tracker_concurrent_primitives.h"
// #include "RAllocator.hpp"
#include "ds_tracker_base.h"


template<class T>
class RangeTrackerNew : public BaseTracker<T> {
 private:
  int task_num;
  int freq;
  int epochFreq;
  bool collect;

 public:
  class IntervalInfo {
   public:
    T *obj;
    uint64_t birth_epoch;
    uint64_t retire_epoch;

    IntervalInfo(T *obj, uint64_t b_epoch, uint64_t r_epoch) :
        obj(obj), birth_epoch(b_epoch), retire_epoch(r_epoch) {}
  };

 private:
  PaddedAtomic<uint64_t> *upper_reservs;
  PaddedAtomic<uint64_t> *lower_reservs;
  Padded<uint64_t> *retire_counters;
  Padded<uint64_t> *alloc_counters;
  Padded<std::list<IntervalInfo>> *retired;

  std::atomic<uint64_t> epoch;

 public:
  ~RangeTrackerNew() {
    for (int i = 0; i < task_num; i++) {
      while (!retired[i].ui.empty()) {
        auto node = retired[i].ui.front().obj;
        retired[i].ui.pop_front();
        free(node);
      }
    }
    delete[] retired;
    delete[] upper_reservs;
    delete[] lower_reservs;
    delete[] retire_counters;
    delete[] alloc_counters;
  }

  RangeTrackerNew(int task_num, int epochFreq, int emptyFreq, bool collect) :
      BaseTracker<T>(task_num), task_num(task_num), freq(emptyFreq), epochFreq(epochFreq), collect(collect) {
    retired = new Padded<std::list<RangeTrackerNew<T>::IntervalInfo>>[task_num];
    upper_reservs = new PaddedAtomic<uint64_t>[task_num];
    lower_reservs = new PaddedAtomic<uint64_t>[task_num];
    for (int i = 0; i < task_num; i++) {
      upper_reservs[i].ui.store(UINT64_MAX, std::memory_order_release);
      lower_reservs[i].ui.store(UINT64_MAX, std::memory_order_release);
    }
    retire_counters = new Padded<uint64_t>[task_num];
    alloc_counters = new Padded<uint64_t>[task_num];
    epoch.store(0, std::memory_order_release);
  }

  RangeTrackerNew(int task_num, int epochFreq, int emptyFreq) : RangeTrackerNew(task_num, epochFreq, emptyFreq, true) {}

  void __attribute__((deprecated)) reserve(uint64_t e, int tid) {
    return reserve(tid);
  }

  uint64_t get_epoch() {
    return epoch.load(std::memory_order_acquire);
  }

  void *alloc(int tid) {
    alloc_counters[tid] = alloc_counters[tid] + 1;
    if (alloc_counters[tid] % (epochFreq * task_num) == 0) {
      epoch.fetch_add(1, std::memory_order_acq_rel);
    }
    // char *block = (char *) malloc(sizeof(uint64_t) + sizeof(T));
    char *block = reinterpret_cast<char *>(malloc(sizeof(uint64_t) + sizeof(T)));
    // uint64_t *birth_epoch = (uint64_t *) (block + sizeof(T));
    uint64_t *birth_epoch = reinterpret_cast<uint64_t *>(block + sizeof(T));
    *birth_epoch = get_epoch();
    // return (void *) block;
    return reinterpret_cast<void *>(block);
  }

  static uint64_t read_birth(T *obj) {
    // uint64_t *birth_epoch = (uint64_t *) ((char *) obj + sizeof(T));
    uint64_t *birth_epoch = reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(obj) + sizeof(T));
    return *birth_epoch;
  }

  void reclaim(T *obj) {
    obj->~T();
    // free((char *) obj);
    free(reinterpret_cast<char *>(obj));
  }

  T *read(std::atomic<T *> const &obj, int idx, int tid) {
    return read(obj, tid);
  }

  T *read(std::atomic<T *> const &obj, int tid) {
    uint64_t prev_epoch = upper_reservs[tid].ui.load(std::memory_order_acquire);
    while (true) {
      T *ptr = obj.load(std::memory_order_acquire);
      uint64_t curr_epoch = get_epoch();
      if (curr_epoch == prev_epoch) {
        return ptr;
      } else {
        // upper_reservs[tid].ui.store(curr_epoch, std::memory_order_release);
        upper_reservs[tid].ui.store(curr_epoch, std::memory_order_seq_cst);
        prev_epoch = curr_epoch;
      }
    }
  }

  void start_op(int tid) {
    uint64_t e = epoch.load(std::memory_order_acquire);
    lower_reservs[tid].ui.store(e, std::memory_order_seq_cst);
    upper_reservs[tid].ui.store(e, std::memory_order_seq_cst);
    // lower_reservs[tid].ui.store(e,std::memory_order_release);
    // upper_reservs[tid].ui.store(e,std::memory_order_release);
  }

  void end_op(int tid) {
    upper_reservs[tid].ui.store(UINT64_MAX, std::memory_order_release);
    lower_reservs[tid].ui.store(UINT64_MAX, std::memory_order_release);
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

  void retire(T *obj, uint64_t birth_epoch, int tid) {
    if (obj == NULL) { return; }
    std::list<IntervalInfo> *myTrash = &(retired[tid].ui);
    // for(auto it = myTrash->begin(); it!=myTrash->end(); it++){
    // assert(it->obj!=obj && "double retire error");
    // }

    uint64_t retire_epoch = epoch.load(std::memory_order_acquire);
    myTrash->push_back(IntervalInfo(obj, birth_epoch, retire_epoch));
    retire_counters[tid] = retire_counters[tid] + 1;
    if (collect && retire_counters[tid] % freq == 0) {
      empty(tid);
    }
  }

  void retire(T *obj, int tid) {
    retire(obj, read_birth(obj), tid);
  }

  bool conflict(std::vector<uint64_t> const &lower_epochs, std::vector<uint64_t> const &upper_epochs,
                uint64_t birth_epoch, uint64_t retire_epoch) {
    for (int i = 0; i < task_num; i++) {
      if (upper_epochs[i] >= birth_epoch && lower_epochs[i] <= retire_epoch) {
        return true;
      }
    }
    return false;
  }

  void empty(int tid) {
    // read all epochs
    std::vector<uint64_t> upper_epochs_arr(task_num, -1);
    std::vector<uint64_t> lower_epochs_arr(task_num, -1);

    for (int i = 0; i < task_num; i++) {
      // sequence matters.
      lower_epochs_arr[i] = lower_reservs[i].ui.load(std::memory_order_acquire);
      upper_epochs_arr[i] = upper_reservs[i].ui.load(std::memory_order_acquire);
    }

    // erase safe objects
    std::list<IntervalInfo> *myTrash = &(retired[tid].ui);
    for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;) {
      IntervalInfo res = *iterator;
      if (!conflict(lower_epochs_arr, upper_epochs_arr, res.birth_epoch, res.retire_epoch)) {
        reclaim(res.obj);
        this->dec_retired(tid);
        iterator = myTrash->erase(iterator);
      } else { ++iterator; }
    }
  }

  bool collecting() { return collect; }


  // for test only
 public:
  int checkmember() {
    return epoch;
  }

  int check_retired_count(int tid) {
    return retired[tid].ui.size();
  }

  bool check_reservation(T *ptr, int slot, int tid) {
    bool correctness = true;
    for (int i = 0; i < task_num; i++) {
      if (lower_reservs[i].ui.load() == 1 && upper_reservs[i].ui.load() != 1) {
        correctness = false;
        break;
      }
    }
    return correctness;
  }
};

//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_RANGE_NEW_H_
