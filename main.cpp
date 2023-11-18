#include <iostream>
#include "ds_lockfree_linkedlist_with_tracker.h"
int main() {
    KWDBLockFreeLinkedListWithTracker<int> list(8, BOA);

    list.Insert(1,1);
    list.Insert(2,1);
    list.Insert(0, 1);
//    std::cout << list.Find(0, 1);



    return 0;
}
