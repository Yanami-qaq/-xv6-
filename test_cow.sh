#!/bin/bash

echo "编译xv6..."
make clean
make

echo "运行COW测试..."
python3 grade-lab-cow

echo "测试完成"
