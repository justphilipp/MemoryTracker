// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_TRACKER_OA_H
#define COMMON_SRC_H_DS_TRACKER_OA_H

//#ifdef LOCKFREE_TRACKER

//#include <cstring>
#include "ds_tracker_concurrent_primitives.h"
#include "ds_tracker_base.h"


template<typename T>
struct NodePtr {
  T *ptr;

  NodePtr() = default;

  NodePtr(NodePtr const &p) = default;

  explicit NodePtr(T *p) : ptr(p) {}
};

template<typename T>
class FreelistStack {
  template<typename> friend
  class OATracker;  /*empty*/

  struct FreelistNode {
    NodePtr<FreelistNode> next;
  };

  typedef NodePtr<FreelistNode> FreelistNodePtr;

  class PoolImpl {
   public:
    FreelistNodePtr pool_;
    uint64_t version_;

    PoolImpl() : pool_(nullptr), version_(0) {}

    PoolImpl(FreelistNode *node) : pool_(node), version_(0) {}

    PoolImpl(FreelistNode *node, uint64_t new_ver) : pool_(FreelistNodePtr(node)), version_(new_ver) {}

    FreelistNode *GetPtr() const { return pool_.ptr; }
  };

 public:
  std::atomic<PoolImpl> pool_impl_;

  explicit FreelistStack(std::size_t n = 0)
      : pool_impl_(nullptr) {
    Reserve(n);
  }

  bool Empty() {
    return pool_impl_.load().GetPtr() == nullptr;
  }

  bool AddReady(T *n) {
    void *node = n;
    PoolImpl old_pool = pool_impl_.load(std::memory_order_consume);

    auto *new_pool_ptr = reinterpret_cast<FreelistNode *>(node);

    for (;;) {
      PoolImpl new_pool(new_pool_ptr, old_pool.version_);
      new_pool.GetPtr()->next = old_pool.pool_;

      if (pool_impl_.compare_exchange_weak(old_pool, new_pool))
        return true;
    }
  }

  bool Add(T *n, uint64_t version) {
    void *node = n;
    PoolImpl old_pool = pool_impl_.load(std::memory_order_consume);
    if (version != old_pool.version_) {
      return false;
    }
    auto *new_pool_ptr = reinterpret_cast<FreelistNode *>(node);

    for (;;) {
      PoolImpl new_pool(new_pool_ptr, version);
      new_pool.GetPtr()->next = old_pool.pool_;

      if (pool_impl_.compare_exchange_weak(old_pool, new_pool))
        return true;
    }
  }

  T *Pop() {
    PoolImpl old_pool = pool_impl_.load(std::memory_order_relaxed);

    // TODO(Bohan Wang) : remember to handle potential deadlock here
    while (true) {
      if (!old_pool.GetPtr()) {
        return nullptr;
      }

      // freelist_node *new_pool_ptr = old_pool.pool_->next;
      FreelistNode *new_pool_ptr = (old_pool.GetPtr())->next.ptr;

      PoolImpl new_pool(new_pool_ptr, old_pool.version_);
      if (pool_impl_.compare_exchange_weak(old_pool, new_pool)) {
        void *ptr = old_pool.GetPtr();
        return reinterpret_cast<T *>(ptr);
      }
      old_pool = pool_impl_.load(std::memory_order_relaxed);
    }
  }

  void Reserve(std::size_t count) {
    for (std::size_t i = 0; i != count; ++i) {
      T *node = reinterpret_cast<T *>(malloc(sizeof(T)));
      DeallocateUnsafe(node);
    }
  }

  ~FreelistStack() {
    FreelistNodePtr current = pool_impl_.load().pool_;
//    freelist_node *current = pool_impl_.load().pool_.ptr;

    while (current.ptr) {
      FreelistNode *current_ptr = current.ptr;
//      T *current_ptr = reinterpret_cast<T *>(current);
      if (current_ptr)
        current = current_ptr->next;
      T *current_ptr_handle = reinterpret_cast<T *>(current_ptr);
      free(current_ptr_handle);
    }
  }

  void DeallocateUnsafe(T *n) {
    void *node = n;
    PoolImpl old_pool = pool_impl_.load(std::memory_order_relaxed);
    auto *new_pool_ptr = reinterpret_cast<FreelistNode *>(node);
    new_pool_ptr->next = old_pool.pool_;
    PoolImpl new_pool(new_pool_ptr, old_pool.version_);
    pool_impl_.store(new_pool);
  }
};


template<class T>
class OATracker : public BaseTracker<T> {
 private:
  int task_num;
  int epoch_freq;
  int empty_freq;
  bool collect;

  /** thread.warning bit
   * a per-thread warning bit, is used to warn a thread that a concurrent recycling has started.
   * If a thread reads true of its warning bit, then its state may contain a stale value. It therefore starts from
   * scratch.
   * On the other hand, if it reads false, then its read is safe(no stale value, id est no concurrent recycling).
  */
  std::vector<bool> warnings;


  // hazard pointer to protect write to reclaimed memory
  uint64_t slots_per_thread;
  PaddedAtomic<T *> *slots;


  /// reclaim related members
  // localver: contains the version that the thread thinks it is helping
  // the thread uses this variable whenever it adds or removes objects to or from the pool
  Padded<uint64_t> *local_vers;


  FreelistStack<T> retire_pool_;
  FreelistStack<T> processing_pool_;
  FreelistStack<T> ready_pool_;

 public:
  OATracker(int task_num, int slotsPerThread, int epochFreq, int emptyFreq, bool collect) : BaseTracker<T>(task_num) {
    this->task_num = task_num;
    warnings.resize(task_num, false);

    this->slots_per_thread = slotsPerThread;
    slots = new PaddedAtomic<T *>[task_num * slotsPerThread];
    for (int i = 0; i < task_num * slotsPerThread; i++) {
      slots[i] = NULL;
    }

    local_vers = new Padded<uint64_t>[task_num];

    ready_pool_.Reserve(10005);
  }

  ~OATracker() {
    delete[] local_vers;
    delete[] slots;
  }


  void *alloc(int tid) {
    T *obj = nullptr;
    while (!obj) {
      obj = ready_pool_.Pop();
      if (!obj) {
        empty(tid);
      }
    }

    // TODO(Bohan Wang): check coding standard
//    memset(obj, 0, sizeof(obj));
    return reinterpret_cast<void *>(obj);
  }

  void retire(T *obj, int tid) {
    bool res = false;
    do {
      res = retire_pool_.Add(obj, local_vers[tid].ui);
      if (!res) {
        empty(tid);
      }
    } while (!res);
  }

  void reclaim(T *obj, int tid) {
    retire(obj, tid);
  }

  void empty(int tid) {
    int counter = 0;

    // TODO(Bohan Wang): use "using" keywords to simplify codes
    typename FreelistStack<T>::PoolImpl local_retire = retire_pool_.pool_impl_.load();
    typename FreelistStack<T>::PoolImpl local_processing = processing_pool_.pool_impl_.load();

    while (local_retire.version_ == local_vers[tid].ui) {
      typename FreelistStack<T>::PoolImpl local_retire_new = local_retire;
      local_retire_new.version_ = local_vers[tid].ui + 1;
      if (retire_pool_.pool_impl_.compare_exchange_strong(local_retire, local_retire_new)) {
        break;
      } else {
        local_retire = retire_pool_.pool_impl_.load();
        counter++;
      }
    }
    local_retire = retire_pool_.pool_impl_.load();

    // 4
    if (local_retire.version_ == local_vers[tid].ui + 1) {
//      // 5
//      PoolImpl local_retire_new = local_retire;
//      local_retire_new.version_ = local_vers[tid].ui + 1;
//      retire_pool_.pool_impl_.compare_exchange_strong(local_retire, local_retire_new);

      // 6
      typename FreelistStack<T>::PoolImpl local_retire_new = retire_pool_.pool_impl_.load();
      typename FreelistStack<T>::PoolImpl local_retire_second_new = retire_pool_.pool_impl_.load();
      local_retire_second_new.version_ = local_vers[tid].ui + 2;
      processing_pool_.pool_impl_.compare_exchange_strong(local_processing, local_retire_second_new);

      // 7
      typename FreelistStack<T>::PoolImpl local_processing_new;
      local_processing_new.version_ = local_vers[tid].ui + 2;
      retire_pool_.pool_impl_.compare_exchange_strong(local_retire_new, local_processing_new);
    }

    local_vers[tid].ui = local_vers[tid].ui + 2;
    // 10
    if (retire_pool_.pool_impl_.load().version_ > local_vers[tid].ui) {
      return;  // phase already finished
    }
    for (int i = 0; i < task_num; i++) {
      warnings[i] = true;
    }

    // line 15
    // TODO(Bohan Wang) : implement memory fence here
    // ensure warning bits are visible

    // 20
    while (!processing_pool_.Empty()) {
      T *res = processing_pool_.Pop();
      if (res) {
        if (!check_hazard(res)) {
          ready_pool_.AddReady(res);
        } else {
          retire_pool_.Add(res, local_vers[tid].ui);
        }
      }
      // 28
      if (!res) {
        return;  // phase already finished
      }
    }
  }

  bool check_hazard(T *node) {
    bool danger = false;
    for (int i = 0; i < task_num * slots_per_thread; i++) {
      if (node == slots[i].ui) {
        danger = true;
        break;
      }
    }
    return danger;
  }

  bool checkThreadWarning(int tid) {
    return warnings[tid];
  }

  void resetThreadWarning(int tid) {
    warnings[tid] = false;
  }

  void oa_read(std::atomic<T *> const &obj, int idx, int tid) {
    T *ret;
    T *realptr;
    ret = obj.load(std::memory_order_acquire);
    // realptr = (T *) ((size_t) ret & 0xfffffffffffffffc);
    realptr = reinterpret_cast<T *>(reinterpret_cast<size_t>(ret) & 0xfffffffffffffffc);
    reserve(realptr, idx, tid);
  }


  void reserve(T *ptr, int slot, int tid) {
    slots[tid * slots_per_thread + slot] = ptr;
  }

  void oa_clear(int tid) {
    for (int i = 0; i < slots_per_thread; i++) {
      slots[tid * slots_per_thread + i] = NULL;
    }
  }
};


//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_OA_H
