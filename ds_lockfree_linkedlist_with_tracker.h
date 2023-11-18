// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.

#ifndef COMMON_SRC_H_DS_LOCKFREE_LINKEDLIST_WITH_TRACKER_H_
#define COMMON_SRC_H_DS_LOCKFREE_LINKEDLIST_WITH_TRACKER_H_

//#ifdef LOCKFREE_TRACKER

#include <stdlib.h>
#include <cstdio>
#include <atomic>
#include <functional>
#include <iostream>
#include <utility>
//#include "ds_hazard_pointer.h"
#include "ds_tracker_concurrent_primitives.h"
#include "ds_tracker_hazard.h"
#include "ds_tracker.h"
#include "ds_tracker_retired_monitorable.h"

#ifdef NGC
#define COLLECT false
#else
#define COLLECT true
#endif


template<typename T>
class KWDBLockFreeLinkedListWithTracker : public RetiredMonitorable {
  static_assert(std::is_copy_constructible<T>());

  struct Node;

  struct MarkPtr {
    std::atomic<Node *> ptr;

    explicit MarkPtr(Node *n) : ptr(n) {}

    MarkPtr() : ptr(nullptr) {}
  };

  struct Node {
    Node() {}

    template<typename... Args>
    explicit Node(Args &&... args)
        : data(T(std::forward<Args>(args)...)), next(nullptr) {}

    Node(T t, Node *n) : data(t), next(n) {}

    T data;
    MarkPtr next;
  };

 private:
  int thread_num;
  MemoryTracker<Node> *memory_tracker;

  Padded<MarkPtr> *head_;
  std::atomic<size_t> size_;

  const size_t GET_POINTER_BITS = 0xfffffffffffffffe;

  inline Node *getPtr(Node *mptr) {
    // return (Node *) ((size_t) mptr & GET_POINTER_BITS);
    return reinterpret_cast<Node *>(reinterpret_cast<size_t>(mptr) & GET_POINTER_BITS);
  }

  inline bool getMk(Node *mptr) {
    // return (bool) ((size_t) mptr & 1);
    return static_cast<bool>(reinterpret_cast<uintptr_t>(mptr) & 1);
  }

  inline Node *mixPtrMk(Node *ptr, bool mk) {
    // return (Node *) ((size_t) ptr | mk);
    return reinterpret_cast<Node *>(reinterpret_cast<size_t>(ptr) | mk);
  }

  inline Node *setMk(Node *mptr) {
    return mixPtrMk(mptr, true);
  }

 public:
  explicit KWDBLockFreeLinkedListWithTracker(int task_num, TrackerType tracker_type = Range_new) :
      RetiredMonitorable(task_num),
      thread_num(task_num),
      head_(new Padded<MarkPtr>),
      size_(0) {
    int epochf = 150;
    int emptyf = 30;
    memory_tracker = new MemoryTracker<Node>(task_num, tracker_type, epochf, emptyf, 3, COLLECT);
  }


  KWDBLockFreeLinkedListWithTracker(const KWDBLockFreeLinkedListWithTracker &other) = delete;

  KWDBLockFreeLinkedListWithTracker(KWDBLockFreeLinkedListWithTracker &&other) = delete;

  KWDBLockFreeLinkedListWithTracker &operator=(const KWDBLockFreeLinkedListWithTracker &other) = delete;

  KWDBLockFreeLinkedListWithTracker &operator=(KWDBLockFreeLinkedListWithTracker &&other) = delete;

  ~KWDBLockFreeLinkedListWithTracker() {
    Node *cur = head_->ui.ptr;
    while (cur != nullptr) {
      Node *temp = cur;
      cur = cur->next.ptr;
      free(temp);
    }
    delete head_;
    delete memory_tracker;
  }


  template<typename... Args>
  Node *mkNode(int tid, Args &&... args) {
    void *ptr = memory_tracker->alloc(tid);
    return new(ptr) Node(std::forward<Args>(args)...);
  }


  // Find the first node which data is greater than the given data,
  // then insert the new node before it then return true, else return false
  // if data already exists in list.
  template<typename... Args>
  bool Emplace(int tid, Args &&... args);

  bool Insert(const T &data, int tid) {
    static_assert(std::is_copy_constructible<T>::value,
                  "T must be copy constructible");
    return Emplace(tid, data);
  }

  bool Insert(T &&data, int tid) {
    static_assert(std::is_constructible<T, T &&>(),
                  "T must be copy constructible");
    return Emplace(tid, std::forward<T>(data));
  }

  // Find the first node which data is equals to the given data,
  // then delete it and return true, if not found the given data then
  // return false.
  bool Delete(const T &data, int tid);

  // Find the first node which data is equals to the given data, if not found
  // the given data then return false.
  bool Find(T data, int tid) {
    MarkPtr *prev = nullptr;
    Node *cur = nullptr;
    Node *nxt = nullptr;
    bool res;

    collect_retired_size(memory_tracker->get_retired_cnt(tid), tid);
    memory_tracker->start_op(tid);

    res = Search(data, &prev, &cur, &nxt, tid);

    memory_tracker->clear_all(tid);
    memory_tracker->end_op(tid);

    return res;
  }

  // Get size of the list.
  size_t size() const {
    return size_.load(std::memory_order_relaxed);
  }

 private:
  bool Search(const T &data, MarkPtr **prev, Node **cur, Node **nxt, int tid);
};


template<typename T>
template<typename... Args>
bool KWDBLockFreeLinkedListWithTracker<T>::Emplace(int tid, Args &&... args) {
  Node *insert_node = nullptr;
  MarkPtr *prev = nullptr;
  Node *cur = nullptr;
  Node *next = nullptr;
  bool res = false;
  insert_node = mkNode(tid, std::forward<Args>(args)...);

  collect_retired_size(memory_tracker->get_retired_cnt(tid), tid);
  memory_tracker->start_op(tid);
  while (true) {
    if (Search(insert_node->data, &prev, &cur, &next, tid)) {
      res = false;
      memory_tracker->reclaim(insert_node, tid);
      break;
    } else {  // does not exist, insert.

      // OA
      memory_tracker->oa_read(prev->ptr, 0, tid);
      memory_tracker->oa_read(cur, 1, tid);
//      memory_tracker->oa_read(next, 2, tid);

      insert_node->next.ptr.store(cur, std::memory_order_release);
      if (prev->ptr.compare_exchange_strong(cur, insert_node, std::memory_order_acq_rel)) {
        res = true;
        size_.fetch_add(1, std::memory_order_relaxed);
        break;
      }
    }
  }

  memory_tracker->end_op(tid);
  memory_tracker->oa_clear(tid);
  memory_tracker->clear_all(tid);

  return res;
}

template<typename T>
bool KWDBLockFreeLinkedListWithTracker<T>::Delete(const T &data, int tid) {
  MarkPtr *prev = nullptr;
  Node *cur = nullptr;
  Node *next = nullptr;

  bool res = true;

  collect_retired_size(memory_tracker->get_retired_cnt(tid), tid);
  memory_tracker->start_op(tid);
  while (true) {
    if (!Search(data, &prev, &cur, &next, tid)) {
      res = false;
      break;
    }
    // OA
    memory_tracker->oa_read(prev->ptr, 0, tid);
    memory_tracker->oa_read(cur, 1, tid);
    memory_tracker->oa_read(next, 2, tid);

    if (memory_tracker->checkThreadWarning(tid)) {
      memory_tracker->resetThreadWarning(tid);
      prev = nullptr;
      cur = nullptr;
      next = nullptr;
      memory_tracker->oa_clear(tid);
      continue;
    }

    // a node is marked meaning it'd been logically deleted
    if (cur->next.ptr.compare_exchange_strong(next, setMk(next), std::memory_order_acq_rel)) {
      size_.fetch_sub(1, std::memory_order_relaxed);
    } else {
      continue;
    }

    if (prev->ptr.compare_exchange_strong(cur, next, std::memory_order_acq_rel)) {
      memory_tracker->retire(cur, tid);
    } else {
      // Search can help retire(physically delete) logically deleted nodes;
      Search(data, &prev, &cur, &next, tid);
    }
    break;
  }

  memory_tracker->end_op(tid);
  memory_tracker->clear_all(tid);
  memory_tracker->oa_clear(tid);
  return res;
}

// Find the first node which data is equals to the given data, if not found
// the given data then return false. *cur_ptr point to that node, *prev_ptr is
// the predecessor of that node.
template<typename T>
bool KWDBLockFreeLinkedListWithTracker<T>::Search(const T &data, MarkPtr **prev, Node **cur, Node **nxt, int tid) {
  while (true) {
    bool cmark = false;
    // TODO(Bohan Wang): check here

    *prev = &head_->ui;

    *cur = getPtr(memory_tracker->read((*prev)->ptr, 1, tid));

    while (true) {  // to lock old and cur
      if (*cur == nullptr) return false;
      *nxt = memory_tracker->read((*cur)->next.ptr, 0, tid);
      // OA: read
      // TODO(Bohan Wang): implement compiler fence here
      if (memory_tracker->checkThreadWarning(tid)) {
        memory_tracker->resetThreadWarning(tid);
        break;
      }
      cmark = getMk(*nxt);
      *nxt = getPtr(*nxt);
      if (mixPtrMk(*nxt, cmark) != memory_tracker->read((*cur)->next.ptr, 1, tid))
        break;  // return findNode(prev,cur,nxt,key,tid);
      auto cdata = (*cur)->data;
      if (memory_tracker->read((*prev)->ptr, 2, tid) != *cur)
        break;
      if (!cmark) {
        if (cdata >= data) return cdata == data;
        *prev = &((*cur)->next);
      } else {
        if ((*prev)->ptr.compare_exchange_strong(*cur, *nxt, std::memory_order_acq_rel))
          memory_tracker->retire(*cur, tid);
        else
          break;
      }
      *cur = *nxt;
    }
  }
}

//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_LOCKFREE_LINKEDLIST_WITH_TRACKER_H_
