// Set default stride to 2 instead of 1
// No need to have a default kernel_size

#include "../src/Kernel.h"
#include "../src/LogicalCube.h"
#include "../src/Layer.h"
#include "../src/config.h"
#include "../src/Connector.h"
#include "../src/bridges/MaxPoolingBridge.h"
#include "test_types.h"
#include "gtest/gtest.h"
#include "glog/logging.h"
#include <iostream>
#include <fstream>
#include <assert.h>
#include <cmath>
#include <cstring>

template <typename TypeParam>
class MaxPoolingBridgeTest : public ::testing::Test {
 public:
  typedef typename TypeParam::T T;	
  MaxPoolingBridgeTest(){
  	data1 = new LogicalCube<T, Layout_CRDB>(iR, iC, iD, mB);
    grad1 = new LogicalCube<T, Layout_CRDB>(iR, iC, iD, mB);
    
    data2 = new LogicalCube<T, Layout_CRDB>(oR, oC, iD, mB);
    grad2 = new LogicalCube<T, Layout_CRDB> (oR,oC, iD, mB);

    layer1 = new Layer<T, Layout_CRDB>(data1, grad1);
    layer2 = new Layer<T, Layout_CRDB>(data2, grad2);

    bconfig = new BridgeConfig(k,s,p);
    
    MaxPoolingBridge_ = new MaxPoolingBridge<T, Layout_CRDB, T, Layout_CRDB>(layer1, layer2, bconfig);
   } 

  	virtual ~MaxPoolingBridgeTest() { delete MaxPoolingBridge_; delete layer1; delete layer2;}
    MaxPoolingBridge<T, Layout_CRDB, T, Layout_CRDB>* MaxPoolingBridge_;

  	LogicalCube<T, Layout_CRDB>* data1;
    LogicalCube<T, Layout_CRDB>* grad1;
    
    LogicalCube<T, Layout_CRDB>* data2;
    LogicalCube<T, Layout_CRDB>* grad2;

    Layer<T, Layout_CRDB>* layer1;
    Layer<T, Layout_CRDB>* layer2;

    BridgeConfig * bconfig;

    const int mB = 2;
    const int iD = 3;
    const int iR = 10;
    const int iC = 10;
    const int oR = 5;
    const int oC = 5;
    const int k = 2;
    const int s = 2;
    const int p = 0;
};

typedef ::testing::Types<FloatCRDB> DataTypes;

TYPED_TEST_CASE(MaxPoolingBridgeTest, DataTypes);

//openblas_set_num_threads -- undefined reference -- currently disabled
TYPED_TEST(MaxPoolingBridgeTest, TestInitialization){
  EXPECT_TRUE(this->MaxPoolingBridge_);
  EXPECT_TRUE(this->layer1);
  EXPECT_TRUE(this->layer2);
  EXPECT_TRUE(this->bconfig);
}

TYPED_TEST(MaxPoolingBridgeTest, TestForward){
	typedef typename TypeParam::T T;
    srand(1);
	for(int i=0;i<this->iR*this->iC*this->iD*this->mB;i++){
        this->data1->p_data[i] = rand()%9;
    }

    this->MaxPoolingBridge_->forward();

    std::fstream expected_output("pooling_forward.txt", std::ios_base::in);
    
    T output;
    int idx = 0;
    if (expected_output.is_open()) {
        expected_output >> output;
        while (!expected_output.eof()) {
            EXPECT_NEAR(this->data2->p_data[idx], output, EPS);
            expected_output >> output;
            idx++;
        }
    }
    expected_output.close();
    // this->data1->logical_print();
    // this->data2->logical_print();
}


TYPED_TEST(MaxPoolingBridgeTest, TestBackward){
    typedef typename TypeParam::T T;
    srand(1);
    for(int i=0;i<this->iR*this->iC*this->iD*this->mB;i++){
        this->data1->p_data[i] = rand()%9;
        this->grad1->p_data[i] = 0;
    }
    
    int oR = (this->iR - this->k)/this->s + 1;
    int oC = (this->iC - this->k)/this->s + 1;

    for(int i=0;i<oR*oC*this->iD*this->mB;i++){
        this->data2->p_data[i] = 1;
        this->grad2->p_data[i] = i;
    }

    this->MaxPoolingBridge_->forward();

    this->MaxPoolingBridge_->backward();

    std::fstream expected_output("pooling_backward.txt", std::ios_base::in);
    
    T output;
    int idx = 0;
    if (expected_output.is_open()) {
        expected_output >> output;
        while (!expected_output.eof()) {
         //   cout << idx << " " << output << " " << this->grad1->p_data[idx] << endl;
            EXPECT_NEAR(this->grad1->p_data[idx], output, EPS);
            expected_output >> output;
            idx++;
        }
    }
    expected_output.close();   
   // this->grad1->logical_print();
}
