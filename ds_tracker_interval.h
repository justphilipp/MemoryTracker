// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_TRACKER_INTERVAL_H_
#define COMMON_SRC_H_DS_TRACKER_INTERVAL_H_

//#ifdef LOCKFREE_TRACKER

#include <queue>
#include <list>
#include <vector>
#include <atomic>

#include "ds_tracker_concurrent_primitives.h"
// #include "RAllocator.hpp"
#include "ds_tracker_base.h"


template<class T>
class IntervalTracker : public BaseTracker<T> {
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
  PaddedAtomic<uint64_t> *reservations;
  Padded<uint64_t> *retire_counters;
  Padded<uint64_t> *alloc_counters;
  Padded<std::list<IntervalInfo>> *retired;

  std::atomic<uint64_t> epoch;

 public:
  ~IntervalTracker() {
    for (int i = 0; i < task_num; i++) {
      while (!retired[i].ui.empty()) {
        auto node = retired[i].ui.front().obj;
        retired[i].ui.pop_front();
        free(node);
      }
    }
    delete[] retired;
    delete[] reservations;
    delete[] retire_counters;
    delete[] alloc_counters;
  }

  IntervalTracker(int task_num, int epochFreq, int emptyFreq, bool collect) :
      BaseTracker<T>(task_num), task_num(task_num), freq(emptyFreq), epochFreq(epochFreq), collect(collect) {
    retired = new Padded<std::list<IntervalTracker<T>::IntervalInfo>>[task_num];
    reservations = new PaddedAtomic<uint64_t>[task_num];
    retire_counters = new Padded<uint64_t>[task_num];
    alloc_counters = new Padded<uint64_t>[task_num];
    for (int i = 0; i < task_num; i++) {
      reservations[i].ui.store(UINT64_MAX, std::memory_order_release);
      retired[i].ui.clear();
    }
    epoch.store(0, std::memory_order_release);
  }

  IntervalTracker(int task_num, int epochFreq, int emptyFreq) : IntervalTracker(task_num, epochFreq, emptyFreq,
                                                                                true) {}


  void __attribute__((deprecated)) reserve(uint64_t e, int tid) {
    return start_op(tid);
  }

  uint64_t getEpoch() {
    return epoch.load(std::memory_order_acquire);
  }

  void *alloc(int tid) {
    alloc_counters[tid] = alloc_counters[tid] + 1;
    if (alloc_counters[tid] % (epochFreq * task_num) == 0) {
      epoch.fetch_add(1, std::memory_order_acq_rel);
    }
    // return (void*)malloc(sizeof(T));
    // char *block = (char *) malloc(sizeof(uint64_t) + sizeof(T));
    char *block = reinterpret_cast<char *>(malloc(sizeof(uint64_t) + sizeof(T)));
    // uint64_t *birth_epoch = (uint64_t *) (block + sizeof(T));
    uint64_t *birth_epoch = reinterpret_cast<uint64_t *>(block + sizeof(T));
    *birth_epoch = getEpoch();
    // return (void *) block;
    return reinterpret_cast<void *>(block);
  }

  uint64_t read_birth(T *obj) {
    // uint64_t *birth_epoch = (uint64_t *) ((char *) obj + sizeof(T));
    uint64_t *birth_epoch = reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(obj) + sizeof(T));
    return *birth_epoch;
  }

  void reclaim(T *obj) {
    obj->~T();
    // free((char *) obj);
    free(reinterpret_cast<char *>(obj));
  }

  void start_op(int tid) {
    uint64_t e = epoch.load(std::memory_order_acquire);
    reservations[tid].ui.store(e, std::memory_order_seq_cst);
  }

  void end_op(int tid) {
    reservations[tid].ui.store(UINT64_MAX, std::memory_order_seq_cst);
  }

  void reserve(int tid) {
    start_op(tid);
  }

  void clear(int tid) {
    end_op(tid);
  }

  bool validate(int tid) {
    return (reservations[tid].ui.load(std::memory_order_acquire) ==
            epoch.load(std::memory_order_acquire));
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
    IntervalInfo info = IntervalInfo(obj, birth_epoch, retire_epoch);
    myTrash->push_back(info);
    retire_counters[tid] = retire_counters[tid] + 1;
    if (collect && retire_counters[tid] % freq == 0) {
      empty(tid);
    }
  }

  void retire(T *obj, int tid) {
    retire(obj, read_birth(obj), tid);
  }

  bool conflict(std::vector<uint64_t> const &reservEpoch, uint64_t birth_epoch, uint64_t retire_epoch) {
    for (int i = 0; i < task_num; i++) {
      if (reservEpoch[i] >= birth_epoch && reservEpoch[i] <= retire_epoch) {
        return true;
      }
    }
    return false;
  }

  void empty(int tid) {
    // read all epochs
    std::vector<uint64_t> reservEpoch(task_num, -1);
    for (int i = 0; i < task_num; i++) {
      reservEpoch[i] = reservations[i].ui.load(std::memory_order_acquire);
    }

    // erase safe objects
    std::list<IntervalInfo> *myTrash = &(retired[tid].ui);
    for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;) {
      IntervalInfo res = *iterator;
      if (!conflict(reservEpoch, res.birth_epoch, res.retire_epoch)) {
        iterator = myTrash->erase(iterator);
        this->reclaim(res.obj);
        this->dec_retired(tid);
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
      if (reservations[i].ui.load() != 1) {
        correctness = false;
        break;
      }
    }
    return correctness;
  }

//    bool check_conflict(int birth_epoch, int retire_epoch) {
//        uint64_t reservEpoch[task_num];
//        for (int i = 0; i < task_num; i++) {
//            reservEpoch[i] = reservations[i].ui.load(std::memory_order_acquire);
//        }
//        conflict(reservEpoch,birth_epoch,retire_epoch);
//    }
};

//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_INTERVAL_H_
