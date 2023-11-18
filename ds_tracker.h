// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_TRACKER_H_
#define COMMON_SRC_H_DS_TRACKER_H_

//#ifdef LOCKFREE_TRACKER

#include <queue>
#include <list>
#include <vector>
#include <atomic>

#include "ds_tracker_concurrent_primitives.h"
#include "ds_tracker_base.h"
#include "ds_tracker_rcu.h"
#include "ds_tracker_interval.h"
#include "ds_tracker_range_new.h"
#include "ds_tracker_hazard.h"
#include "ds_tracker_he.h"
#include "ds_tracker_oa.h"
#include "ds_tracker_boa.h"

// #include "RAllocator.hpp"


#if !(__x86_64__ || __ppc64__)
#include "RangeTrackerTP.hpp"
#endif

// TODO(Bohan Wang) : use enum class
enum TrackerType {
  // for epoch-based trackers.
  NIL = 0,
  RCU = 2,
  Interval = 4,
  Range = 6,
  Range_new = 8,
  QSBR = 10,
  Range_TP = 12,
  // for HP-like trackers.
  Hazard = 1,
  Hazard_dynamic = 3,
  HE = 5,

  // for optimistic trackers.
  OA = 20,
  BOA = 21
};

template<class T>
class MemoryTracker {
 private:
  int task_num_;
  BaseTracker<T> *tracker = NULL;
  TrackerType type = NIL;
  Padded<int *> *slot_renamers = NULL;

 public:
  MemoryTracker(int task_num, TrackerType tracker_type, int epoch_freq, int empty_freq, int slot_num, bool collect) {
//        if (tracker_type == 0) {
//            tracker_type = RCU;
//        }
    task_num_ = task_num;
    slot_renamers = new Padded<int *>[task_num];
    for (int i = 0; i < task_num; i++) {
      slot_renamers[i].ui = new int[slot_num];
      for (int j = 0; j < slot_num; j++) {
        slot_renamers[i].ui[j] = j;
      }
    }
    if (tracker_type == NIL) {
      tracker = new BaseTracker<T>(task_num);
      type = NIL;
    } else if (tracker_type == RCU) {
      tracker = new RCUTracker<T>(task_num, epoch_freq, empty_freq, collect);
      type = RCU;
    } else if (tracker_type == Range_new) {
      tracker = new RangeTrackerNew<T>(task_num, epoch_freq, empty_freq, collect);
      type = Range_new;
    } else if (tracker_type == Hazard) {
      tracker = new HazardTracker<T>(task_num, slot_num, empty_freq, collect);
      type = Hazard;
    } else if (tracker_type == HE) {
      // tracker = new HETracker<T>(task_num, slot_num, 1, collect);
      tracker = new HETracker<T>(task_num, slot_num, epoch_freq, empty_freq, collect);
      type = HE;
    } else if (tracker_type == QSBR) {
      tracker = new RCUTracker<T>(task_num, epoch_freq, empty_freq, type_QSBR, collect);
      type = QSBR;
    } else if (tracker_type == Interval) {
      tracker = new IntervalTracker<T>(task_num, epoch_freq, empty_freq, collect);
      type = Interval;
    } else if (tracker_type == OA) {
      tracker = new OATracker<T>(task_num, slot_num, epoch_freq, empty_freq, collect);
      type = OA;
    } else if (tracker_type == BOA) {
      tracker = new boatracker::BOATracker<T>(task_num, slot_num, epoch_freq, empty_freq, collect);
      type = BOA;
    } else {
      // TODO(Bohan Wang): exception catching mechanism
      // errexit("constructor - tracker type error.");
    }
  }

  // only compile in 32 bit mode
// #if !(__x86_64__ || __ppc64__)
//       else if (tracker_type == "TP"){
//       tracker = new RangeTrackerTP<T>(task_num, epoch_freq, empty_freq, collect);
//       type = Range_TP;
//   }
// #endif


  ~MemoryTracker() {
    delete tracker;
    for (int i = 0; i < task_num_; i++) {
      delete[] slot_renamers[i].ui;
    }
    delete[] slot_renamers;
  }


  void *alloc() {
    return tracker->alloc();
  }

  void *alloc(int tid) {
    return tracker->alloc(tid);
  }

// NOTE: reclaim shall be only used to thread-local objects.
  void reclaim(T *obj) {
    if (obj != nullptr)
      tracker->reclaim(obj);
  }

  void reclaim(T *obj, int tid) {
    if (obj != nullptr)
      tracker->reclaim(obj, tid);
  }

  void start_op(int tid) {
    // tracker->inc_opr(tid);
    tracker->start_op(tid);
  }

  void end_op(int tid) {
    tracker->end_op(tid);
  }

  T *read(std::atomic<T *> const &obj, int idx, int tid) {
    return tracker->read(obj, slot_renamers[tid].ui[idx], tid);
  }

  void transfer(int src_idx, int dst_idx, int tid) {
    int tmp = slot_renamers[tid].ui[src_idx];
    slot_renamers[tid].ui[src_idx] = slot_renamers[tid].ui[dst_idx];
    slot_renamers[tid].ui[dst_idx] = tmp;
  }

  void release(int idx, int tid) {
    tracker->release(slot_renamers[tid].ui[idx], tid);
  }

  void clear_all(int tid) {
    tracker->clear_all(tid);
  }

  void retire(T *obj, int tid) {
    tracker->inc_retired(tid);
    tracker->retire(obj, tid);
  }

  uint64_t get_retired_cnt(int tid) {
    if (type) {
      return tracker->get_retired_cnt(tid);
    } else {
      return 0;
    }
  }

  // for TrackerOA
  bool checkThreadWarning(int tid) {
    return tracker->checkThreadWarning(tid);
  }

  void resetThreadWarning(int tid) {
    tracker->resetThreadWarning(tid);
  }

  void oa_read(std::atomic<T *> const &obj, int idx, int tid) {
    return tracker->oa_read(obj, slot_renamers[tid].ui[idx], tid);
  }

  void oa_clear(int tid) {
    tracker->oa_clear(tid);
  }
// for test only
 public:

  int check_tracker_type() {
    return type;
  }

  int check_slot_renamers(int i, int j) {
    return slot_renamers[i].ui[j];
  }

// test interval, hazard
  int checkmember() {
    return tracker->checkmember();
  }

  int check_retired_count(int tid) {
    return tracker->check_retired_count(tid);
  }

  bool check_reservation(T *ptr, int slot, int tid) {
    return tracker->check_reservation(ptr, slot, tid);
  }

};

//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_H_
