// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_TRACKER_RETIRED_MONITORABLE_H_
#define COMMON_SRC_H_DS_TRACKER_RETIRED_MONITORABLE_H_

//#ifdef LOCKFREE_TRACKER

#include <queue>
#include <list>
#include <vector>
#include <atomic>

#include "ds_tracker_concurrent_primitives.h"
// #include "RAllocator.hpp"

class RetiredMonitorable {
 private:
  Padded<uint64_t> *retired_cnt;
 public:
//    RetiredMonitorable(GlobalTestConfig* gtc){
//        retired_cnt = new padded<uint64_t>[gtc->task_num];
//    }

  explicit RetiredMonitorable(int task_num) {
    retired_cnt = new Padded<uint64_t>[task_num];
  }

  ~RetiredMonitorable() {
  delete[] retired_cnt;
}

  void collect_retired_size(uint64_t size, int tid) {
    retired_cnt[tid].ui += size;
  }

  uint64_t report_retired(int tid) {
    return retired_cnt[tid].ui;
  }
};

//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_RETIRED_MONITORABLE_H_
