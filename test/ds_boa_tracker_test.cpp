// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.


#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <iostream>
#include <random>
#include <thread>
//#include "test_kvstruct.h"
//#include "ds_sorted_unordered_map.h"
#include "../ds_lockfree_linkedlist_with_tracker.h"

namespace zdpts {

class TestBOATracker : public ::testing::Test {
 public:
  TestBOATracker() {}

  virtual void SetUp() {}

  virtual void TearDown() {}

  static void SetUpTestCase() {
    std::cout << "run before first case..." << std::endl;
  }

  static void TearDownTestCase() {
    std::cout << "run after last case..." << std::endl;
  }
};

TEST_F(TestBOATracker, constructor) {
  // testing ctor
  KWDBLockFreeLinkedListWithTracker<int> list(4, BOA);


  // no empty() interface
  // EXPECT_TRUE(map_1.empty());

  // ctor member variables

  EXPECT_EQ(0, list.report_retired(3));
  EXPECT_EQ(0, list.size());

}


TEST_F(TestBOATracker, Insert) {
  // testing insert.
  // existed items shouldn't be successfully inserted, hence the second round of insert should be expected false.
  KWDBLockFreeLinkedListWithTracker<int> list(1, BOA);
//  int size = 5;

  for (int i = 0; i < 5; i++) {
    EXPECT_TRUE(list.Insert(i, 0));
  }
  for (int i = 0; i < 2; i++) {
//    kwdbts::kvstruct kv(i);
    EXPECT_FALSE(list.Insert(i, 0));
  }
}


TEST_F(TestBOATracker, Find) {
  // testing get.

  KWDBLockFreeLinkedListWithTracker<int> list(4, BOA);
  int size = 1000;
  for (int i = 0; i < size; i++) {
    list.Insert(i, 0);
  }

  for (int i = 0; i < size; i++) {
    EXPECT_TRUE(list.Find(i, 0));
  }
  EXPECT_FALSE(list.Find(1001, 0));
}

TEST_F(TestBOATracker, Insert_size) {
  // testing insert and get.
  // if certain item was correctly inserted, it should be successfully got.
  KWDBLockFreeLinkedListWithTracker<int> list(4, BOA);
  int size = 1000;
  EXPECT_EQ(0, list.size());
  for (int i = 0; i < size; i++) {
    list.Insert(i, 0);
    EXPECT_EQ(i + 1, list.size());
  }
  EXPECT_EQ(1000, list.size());
}


TEST_F(TestBOATracker, Delete) {
  // testing remove.
  KWDBLockFreeLinkedListWithTracker<int> list(4, BOA);
  int size = 1000;

  // remove non-existed key-value pairs
  for (int i = 0; i < size; i++) {
    EXPECT_FALSE(list.Delete(i, 0));
  }

  // insert and remove existed key-value pairs
  for (int i = 0; i < 1000; i++) {
    list.Insert(i, 0);
  }
  for (int i = 0; i < 1000; i++) {
    EXPECT_TRUE(list.Delete(i, 0));
  }
  for (int i = 1000; i < 2000; i++) {
    list.Insert(i, 0);
  }
  for (int i = 1000; i < 2000; i++) {
    EXPECT_TRUE(list.Delete(i, 0));
  }
  EXPECT_EQ(0, list.size());
}

TEST_F(TestBOATracker, ConcurrentInsert) {
  KWDBLockFreeLinkedListWithTracker<int> list(16, BOA);

  constexpr int kNumThreads = 16;
  constexpr int kNumItemsPerThread = 50;
  constexpr int ItemLowerBound = 0;
  constexpr int ItemUpperBound = 10000;
  std::default_random_engine e;
  std::uniform_int_distribution<int> u(ItemLowerBound, ItemUpperBound);

  std::atomic<uint64_t> falseCount;
  falseCount = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i]() {
      for (int j = 0; j < kNumItemsPerThread; ++j) {
        int temp = u(e);
        if (list.Find(temp, i)) {
          EXPECT_FALSE(list.Insert(temp, i));
          falseCount.fetch_add(1, std::memory_order_relaxed);
        } else {
          if (!list.Insert(temp, i)) {
            falseCount.fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
    });
  }
  for (auto &thread: threads) {
    thread.join();
  }
  EXPECT_EQ(kNumThreads * kNumItemsPerThread - falseCount, list.size());
}

TEST_F(TestBOATracker, ConcurrentDelete) {
  KWDBLockFreeLinkedListWithTracker<int> list(16, BOA);


  constexpr int kNumThreads = 16;
  constexpr int kNumItemsPerThread = 500;
  constexpr int ItemLowerBound = 0;
  constexpr int ItemUpperBound = 10000;
  constexpr int PreFilled = 10000;
  for (int i = 0; i < PreFilled; i++) {
    list.Insert(i, 0);
  }
  std::default_random_engine e;
  std::uniform_int_distribution<int> u(ItemLowerBound, ItemUpperBound);
  std::atomic<uint64_t> falseCount;
  falseCount = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i]() {
      for (int j = 0; j < kNumItemsPerThread; ++j) {
        int temp = u(e);
        if (!list.Find(temp, i)) {
          EXPECT_FALSE(list.Delete(temp, i));
          falseCount.fetch_add(1, std::memory_order_relaxed);
        } else {
          if (!list.Delete(temp, i)) {
            falseCount.fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
    });
  }
  for (auto &thread: threads) {
    thread.join();
  }
  EXPECT_EQ(kNumThreads * kNumItemsPerThread - falseCount, PreFilled - list.size());
}

TEST_F(TestBOATracker, ThreadSafety) {
  KWDBLockFreeLinkedListWithTracker<int> list(16, BOA);

  constexpr int kNumThreads = 16;
  constexpr int kNumItemsPerThread = 1000;

  std::atomic<uint64_t> listSize;
  listSize = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    if (i % 2 == 0) {
      threads.emplace_back([&list, &listSize, i, start = i * kNumItemsPerThread]() {
        for (int j = start; j < start + kNumItemsPerThread; ++j) {
          if (list.Insert(j, i)) {
            listSize.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    } else {
      threads.emplace_back([&list, &listSize, i]() {
        for (int j = 0; j < kNumItemsPerThread; ++j) {
          if (list.Delete(j, i)) {
            listSize.fetch_sub(1, std::memory_order_relaxed);
          }
        }
      });
    }
  }
  for (auto &thread: threads) {
    thread.join();
  }
  EXPECT_EQ(listSize.load(), list.size());
}


TEST_F(TestBOATracker, check_empty) {
  KWDBLockFreeLinkedListWithTracker<int> list(32, BOA);

  constexpr int kNumThreads = 32;
  constexpr int kNumItemsPerThread = 3000;

  std::random_device seed;
  std::mt19937 engine(seed());
  std::uniform_int_distribution<> distrib(1, 10000);
  std::atomic<uint64_t> listSize;
  std::atomic<uint64_t> insertSize;
  std::atomic<uint64_t> deleteSize;
  listSize = 0;
  insertSize = 0;
  deleteSize = 0;
  std::vector<std::thread> threads;

  for (int i = 0; i < kNumThreads; ++i) {
    if (i % 2 == 0) {
      threads.emplace_back([&, i]() {
        for (int j = 0; j < kNumItemsPerThread; ++j) {
          int random = distrib(engine);
          if (list.Insert(random, i)) {
            insertSize.fetch_add(1, std::memory_order_relaxed);
            listSize.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    } else {
      threads.emplace_back([&, i]() {
        for (int j = 0; j < kNumItemsPerThread; ++j) {
          int random = distrib(engine);
          if (list.Delete(random, i)) {
            deleteSize.fetch_add(1, std::memory_order_relaxed);
            listSize.fetch_sub(1, std::memory_order_relaxed);
          }
        }
      });
    }
  }

  for (auto &thread: threads) {
    thread.join();
  }
  std::cout << insertSize - deleteSize << std::endl;
  EXPECT_EQ(listSize.load(), list.size());
}

TEST_F(TestBOATracker, check_singular_empty) {
  KWDBLockFreeLinkedListWithTracker<int> list(1, BOA);

  constexpr int kNumThreads = 10;
//  constexpr int kNumItemsPerThread = 5000;
  int total = 0;
  std::random_device seed;
  std::mt19937 engine(seed());
  std::uniform_int_distribution<> distrib(0, 1000);
  std::atomic<uint64_t> listSize;
  listSize = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    if (i % 2 == 0) {
      for (int j = 0; j < 1000; ++j) {
        if (list.Insert(j, 0)) {
          listSize.fetch_add(1, std::memory_order_relaxed);
          total++;
        }
      }
    } else {
      for (int j = 0; j < 1000; ++j) {
        if (list.Delete(j, 0)) {
          listSize.fetch_sub(1, std::memory_order_relaxed);
          total++;
        }
      }
    }
  }

  EXPECT_EQ(listSize.load(), list.size());
}

TEST_F(TestBOATracker, FindThreadSafety) {
  KWDBLockFreeLinkedListWithTracker<int> list(16, BOA);

  constexpr int kNumThreads = 16;
  constexpr int kNumItemsPerThread = 1000;

  std::atomic<uint64_t> listSize;
  listSize = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < 10000; i++) {
    list.Insert(i, i % 16);
  }
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&list, &listSize, i, start = i * kNumItemsPerThread]() {
      for (int j = start; j < start + kNumItemsPerThread; ++j) {
        if (j % 2 == 0) {
          list.Insert(j, i);
        } else {
          list.Delete(j, i);
        }
      }
    });
  }
    for (auto &thread: threads) {
      thread.join();
    }
    for (int i = 0; i < 16000; i++) {
      if (i % 2 == 0) {
        EXPECT_TRUE(list.Find(i, i % 16));
      } else {
        EXPECT_FALSE(list.Find(i, i % 16));
      }
    }
  }

  int main(int argc, char *argv[]) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
  }

}  // namespace zdpts


