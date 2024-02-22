
#ifndef COMMON_SRC_H_DS_TRACKER_BOA_H
#define COMMON_SRC_H_DS_TRACKER_BOA_H

//#ifdef LOCKFREE_TRACKER

#include "ds_tracker_concurrent_primitives.h"
#include "ds_tracker_base.h"
#include "ext/ARIMA/ARIMAModel.h"

namespace boatracker {

template<typename T>
struct NodePtr {
  T *ptr;

//  NodePtr() = default;

  NodePtr(NodePtr const &p) = default;

  explicit NodePtr(T *p) : ptr(p) {}

  T *GetPtr() const { return ptr; }

  void SetPtr(T *p) { ptr = p; }

  explicit operator bool() const { return GetPtr() != 0; }

  T *operator->() const { return GetPtr(); }

  bool operator==(NodePtr const &p) const { return ptr == p.ptr; }
};

template<typename T>
class FreelistStack {
  template<typename> friend
  class BOATracker;  /* empty */

  struct FreelistNode {
    NodePtr<FreelistNode> next;
  };

  typedef NodePtr<FreelistNode> FreelistNodePtr;

 public:
  FreelistNodePtr pool_;

  explicit FreelistStack(std::size_t n = 0)
      : pool_(nullptr) {
    Reserve(n);
  }

  bool Empty() {
    return pool_.ptr == nullptr;
  }

  bool AddReady(T *n) {
    void *node = n;
    FreelistNodePtr old_pool = pool_;

    auto *new_pool_ptr = reinterpret_cast<FreelistNode *>(node);
    FreelistNodePtr new_pool(new_pool_ptr);
    new_pool->next = old_pool;
    pool_ = new_pool;
    return true;
  }

  bool Add(T *n) {
    void *node = n;
    FreelistNodePtr old_pool = pool_;

    auto *new_pool_ptr = reinterpret_cast<FreelistNode *>(node);
    FreelistNodePtr new_pool(new_pool_ptr);
    new_pool->next = old_pool;
    pool_ = new_pool;
    return true;
  }

  T *Pop() {
    FreelistNodePtr old_pool = pool_;

    if (!old_pool.GetPtr()) {
      return nullptr;
    }

    // operator->
    FreelistNode *new_pool_ptr = old_pool->next.GetPtr();
    FreelistNodePtr new_pool(new_pool_ptr);

    pool_ = new_pool;
    void *ptr = old_pool.GetPtr();
    return reinterpret_cast<T *>(ptr);
  }

  void Reserve(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
      char *block = reinterpret_cast<char *>(malloc(2 * sizeof(uint64_t) + sizeof(T)));
      auto *birth_epoch = reinterpret_cast<uint64_t *>(block + sizeof(T));
      *birth_epoch = -1;
      auto *retire_epoch = reinterpret_cast<uint64_t *>(block + sizeof(T) + sizeof(uint64_t));
      *retire_epoch = UINT64_MAX;
      DeallocateUnsafe(block);
    }
  }

  ~FreelistStack() {
    FreelistNodePtr current = pool_;
//    freelist_node *current = pool_impl_.load().pool_.ptr;

    while (current) {
      FreelistNode *current_ptr = current.GetPtr();
//      T *current_ptr = reinterpret_cast<T *>(current);
      if (current_ptr)
        current = current_ptr->next;
      char *current_ptr_handle = reinterpret_cast<char *>(current_ptr);
      free(current_ptr_handle);
    }
  }

  void DeallocateUnsafe(char *n) {
    void *node = n;
    FreelistNodePtr old_pool = pool_;
    auto *new_pool_ptr = reinterpret_cast<FreelistNode *>(node);
    new_pool_ptr->next = old_pool;
    FreelistNodePtr new_pool(new_pool_ptr);
    pool_ = new_pool;
  }

  void FreeBlock(char* block){
    free(block);
  }
};


template<class T>
class BOATracker : public BaseTracker<T> {
 private:
  int task_num_;
  int epoch_freq_;
  int empty_freq_;
  bool collect_;

  /** thread.warning bit
   * a per-thread warning bit, is used to warn a thread that a concurrent recycling has started.
   * If a thread reads true of its warning bit, then its state may contain a stale value. It therefore starts from
   * scratch.
   * On the other hand, if it reads false, then its read is safe(no stale value, id est no concurrent recycling).
  */
//  std::vector<bool> warnings;
  PaddedAtomic<bool> *warnings;

  // interval tracker to protect write to reclaimed memory
  PaddedAtomic<uint64_t> *upper_reservs;
  PaddedAtomic<uint64_t> *lower_reservs;
  Padded<uint64_t> *retire_counters;
  Padded<uint64_t> *alloc_counters;
  std::atomic<uint64_t> epoch;

  std::vector<FreelistStack<T>> retire_pool_;
  std::vector<FreelistStack<T>> processing_pool_;
  std::vector<FreelistStack<T>> ready_pool_;

  std::vector<std::pair<uint64_t, uint64_t>> alloc_since_;

  // just fix n = 5 right now
  std::vector<std::list<time_t>> alloc_last_n_mins;
  std::vector<std::list<time_t>> dealloc_last_n_mins;

 public:
  BOATracker(int task_num, int slotsPerThread, int epochFreq, int emptyFreq, bool collect) : BaseTracker<T>(task_num),
                                                                                             task_num_(task_num),
                                                                                             epoch_freq_(epochFreq),
                                                                                             empty_freq_(emptyFreq),
                                                                                             collect_(collect),
                                                                                             epoch(0) {
    task_num_ = task_num;
    warnings = new PaddedAtomic<bool>[task_num];
    upper_reservs = new PaddedAtomic<uint64_t>[task_num];
    lower_reservs = new PaddedAtomic<uint64_t>[task_num];
    for (int i = 0; i < task_num; i++) {
      upper_reservs[i].ui.store(UINT64_MAX, std::memory_order_release);
      lower_reservs[i].ui.store(UINT64_MAX, std::memory_order_release);
    }
    retire_counters = new Padded<uint64_t>[task_num];
    alloc_counters = new Padded<uint64_t>[task_num];
//    epoch.store(0, std::memory_order_release);
    retire_pool_.resize(task_num);
    processing_pool_.resize(task_num);
    ready_pool_.resize(task_num);

    for (auto &pool: ready_pool_) {
      pool.Reserve(2001);
    }

    // LB
    // 0 -> alloc since last time
    // 1 -> dealloc since last time
    // std::vector<std::vector<std::queue<uint64_t>>> alloc_since(task_num_, std::vector<std::queue<uint64_t>>(2));
    alloc_since_.resize(task_num_);
    alloc_last_n_mins.resize(task_num_);
    dealloc_last_n_mins.resize(task_num_);
  }

  ~BOATracker() {
    delete[] upper_reservs;
    delete[] lower_reservs;
    delete[] warnings;
    delete[] retire_counters;
    delete[] alloc_counters;
  }


  void *alloc(int tid) {
    alloc_counters[tid] = alloc_counters[tid] + 1;
    if (alloc_counters[tid] % (epoch_freq_ * task_num_) == 0) {
      epoch.fetch_add(1, std::memory_order_acq_rel);
    }
    void *obj = nullptr;
    int reclaimed_counter = 0;
    while (!(obj = ready_pool_[tid].Pop())) {
      // obj = ready_pool_[tid].Pop();
      empty(tid);
      reclaimed_counter++;
      if (reclaimed_counter >= 2) {
        ready_pool_[tid].Reserve(1);
      }
    }
    alloc_since_[tid].first++;
    alloc_last_n_mins[tid].push_back(time(nullptr));
    auto node = reinterpret_cast<char *>(obj);
    auto *birth_epoch = reinterpret_cast<uint64_t *>(node + sizeof(T));
    *birth_epoch = get_epoch();

    // TODO: check coding standard
//    memset(obj, 0, sizeof(obj));
    return reinterpret_cast<void *>(obj);
  }

  void retire(T *obj, int tid) {
    write_retire(obj);
    retire_pool_[tid].Add(obj);
    alloc_since_[tid].second++;
    dealloc_last_n_mins[tid].push_back(time(nullptr));
  }

  void reclaim(T *obj, int tid) {
    retire(obj, tid);
  }

  void empty(int tid) {
    // uint64_t reserve_cnt = HandleReserveStrawman(tid);

    uint64_t reserve_cnt = HandleReserveARIMA(tid, 5);
    for (int i = 0; i < task_num_; i++) {
      warnings[i] = true;
    }

    // TODO : implement memory fence here
    // ensure warning bits are visible

    FreelistStack<T> new_retire = processing_pool_[tid];
    processing_pool_[tid] = retire_pool_[tid];
    retire_pool_[tid] = new_retire;

    T *res = nullptr;
    while ((res = processing_pool_[tid].Pop())) {
      // T *res = processing_pool_[tid].Pop();
      if (!check_conflict(res)) {
        if (reserve_cnt > 0) {
          ready_pool_[tid].AddReady(res);
          reserve_cnt--;
        } else {  // pool[tid] has already reserved enough blocks
          processing_pool_[tid].FreeBlock((char *) res);
        }
      } else {
        retire_pool_[tid].Add(res);
      }
    }
    if(reserve_cnt > 0){  // ready_pool_[tid] needs to reserve more
      ready_pool_[tid].Reserve(reserve_cnt);
    }
  }

  uint64_t get_epoch() {
    return epoch.load(std::memory_order_acquire);
  }

  bool check_conflict(T *node) {
    auto ptr = reinterpret_cast<char *>(node);
    auto birth_epoch = reinterpret_cast<uint64_t *>(ptr + sizeof(T));
    uint64_t birth_e = *birth_epoch;
    auto retire_epoch = reinterpret_cast<uint64_t *>(ptr + sizeof(T) + sizeof(uint64_t));
    uint64_t retire_e = *retire_epoch;
    for (int i = 0; i < task_num_; i++) {
      if (upper_reservs[i].ui >= birth_e && lower_reservs[i].ui <= retire_e) {
        return true;
      }
    }
    return false;
  }

  void write_retire(T *obj) {
    auto *retire_epoch = reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(obj) + sizeof(T) + sizeof(uint64_t));
    *retire_epoch = get_epoch();
  }

  bool checkThreadWarning(int tid) {
    return warnings[tid];
  }

  void resetThreadWarning(int tid) {
    warnings[tid] = false;
  }

//  void oa_read(std::atomic<T *> const &obj, int idx, int tid) {
//    T *ret;
//    T *realptr;
//    ret = obj.load(std::memory_order_acquire);
//    // realptr = (T *) ((size_t) ret & 0xfffffffffffffffc);
//    realptr = reinterpret_cast<T *>(reinterpret_cast<size_t>(ret) & 0xfffffffffffffffc);
//    reserve(realptr, idx, tid);
//  }

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

  uint64_t HandleReserveStrawman(int tid){
    uint64_t res;
    float percentage = (float)alloc_since_[tid].first / (float)(alloc_since_[tid].second + 1);  // divide by zero
    res = alloc_since_[tid].first * 2 * (uint64_t)percentage;
    return res;
  }

  uint64_t HandleReserveARIMA(int tid, int nmins){
    uint64_t res;
    std::vector<double> tsdata(nmins);
    time_t nowtime = time(nullptr);

    auto alloclist = alloc_last_n_mins[tid];
    for (auto it = alloclist.begin(); it != alloclist.end();) {
      if ((nowtime - *it) >= nmins * 60) {
        it = alloclist.erase(it);
      } else {
        int index = nmins - 1 - (nowtime - *it) / 60;
        tsdata[index]++;
        it++;
      }
    }

    auto dealloclist = dealloc_last_n_mins[tid];
    for (auto it = dealloclist.begin(); it != dealloclist.end();) {
      if ((nowtime - *it) >= nmins * 60) {
        it = dealloclist.erase(it);
      } else {
        int index = nmins - 1 - (nowtime - *it) / 60;
        tsdata[index]--;
        it++;
      }
    }
//    for (auto num : tsdata){
//      std::cout << num << " ";
//    }
//    std::cout << std::endl;

    auto *arima = new ARIMAModel(tsdata);

    int period = 1;
    int modelCnt = 5;
    int cnt = 0;
    std::vector<std::vector<int>> list;
    std::vector<int> tmpPredict(modelCnt);

    for (int k = 0; k < modelCnt; ++k)      //控制通过多少组参数进行计算最终的结果
    {
      std::vector<int> bestModel = arima->getARIMAModel(period, list, k != 0);
      //std::cout<<bestModel.size()<<std::endl;

      if (bestModel.empty()) {
        tmpPredict[k] = (int) tsdata[tsdata.size() - period];
        cnt++;
        break;
      } else {
        //std::cout<<bestModel[0]<<bestModel[1]<<std::endl;
        int predictDiff = arima->predictValue(bestModel[0], bestModel[1], period);
        //std::cout<<"fuck"<<std::endl;
        tmpPredict[k] = arima->aftDeal(predictDiff, period);
        cnt++;
      }
      // std::cout << bestModel[0] << " " << bestModel[1] << std::endl;
      list.push_back(bestModel);
    }

    double sumPredict = 0.0;
    for (int k = 0; k < cnt; ++k) {
      sumPredict += ((double) tmpPredict[k]) / (double) cnt;
    }
    int predict = (int) std::round(sumPredict);
//    std::cout << "Predict value=" << predict << std::endl;

    delete arima;
    return predict;
  }
};

}  // namespace boatracker


//#endif  // LOCKFREE_TRACKER

#endif  // COMMON_SRC_H_DS_TRACKER_BOA_H
