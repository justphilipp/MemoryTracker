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

class TestARIMA : public ::testing::Test {
public:
  TestARIMA() {}

  virtual void SetUp() {}

  virtual void TearDown() {}

  static void SetUpTestCase() {
    std::cout << "run before first case..." << std::endl;
  }

  static void TearDownTestCase() {
    std::cout << "run after last case..." << std::endl;
  }
};

TEST_F(TestARIMA, ARIMAIntegration) {
  KWDBLockFreeLinkedListWithTracker<int> list(4, BOA);

  constexpr int kNumThreads = 4;
  constexpr int ItemLowerBound = 0;
  constexpr int ItemUpperBound = 10000;

  std::default_random_engine e;
  std::uniform_int_distribution<int> u(ItemLowerBound, ItemUpperBound);
  std::uniform_int_distribution<int> index(0,3);
  std::uniform_int_distribution<int> ops(0,2);
  time_t start = time(nullptr);
  while(time(nullptr) - start < 360){
    int value = u(e);
    int idx = index(e);
    int op = ops(e);
    if(op == 2){
      list.Delete(value, idx);
    } else {
      list.Insert(value, idx);
    }
  }
  std::cout << "timeout" << std::endl;
}


int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

}  // namespace zdpts


