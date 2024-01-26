#include <iostream>
#include <vector>
#include "ds_lockfree_linkedlist_with_tracker.h"
#include "ext/ARIMA/ARIMAModel.h"

int main() {
  KWDBLockFreeLinkedListWithTracker<int> lists(8, BOA);

  lists.Insert(1, 1);
  lists.Insert(2, 1);
  lists.Insert(0, 1);
  std::cout << lists.Find(0, 1);

  freopen("/home/tonghua/Tracker/tsdata.txt", "r", stdin);
  double gets;
  std::vector<double> dataArray;
  while (std::cin >> gets) {
    dataArray.push_back(gets);
    std::cout << gets << std::endl;
  }

  ARIMAModel *arima = new ARIMAModel(dataArray);


  int period = 7;
  int modelCnt = 5;
  int cnt = 0;
  std::vector<std::vector<int>> list;
  std::vector<int> tmpPredict(modelCnt);

  for (int k = 0; k < modelCnt; ++k)      //控制通过多少组参数进行计算最终的结果
  {
    std::vector<int> bestModel = arima->getARIMAModel(period, list, (k == 0) ? false : true);
    //std::cout<<bestModel.size()<<std::endl;

    if (bestModel.size() == 0) {
      tmpPredict[k] = (int) dataArray[dataArray.size() - period];
      cnt++;
      break;
    } else {
      //std::cout<<bestModel[0]<<bestModel[1]<<std::endl;
      int predictDiff = arima->predictValue(bestModel[0], bestModel[1], period);
      //std::cout<<"fuck"<<std::endl;
      tmpPredict[k] = arima->aftDeal(predictDiff, period);
      cnt++;
    }
    std::cout << bestModel[0] << " " << bestModel[1] << std::endl;
    list.push_back(bestModel);
  }

  double sumPredict = 0.0;
  for (int k = 0; k < cnt; ++k) {
    sumPredict += ((double) tmpPredict[k]) / (double) cnt;
  }
  int predict = (int) std::round(sumPredict);
  std::cout << "Predict value=" << predict << std::endl;


  return 0;
}
