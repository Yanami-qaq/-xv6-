#!/bin/bash

echo "启动qemu并运行bttest..."
timeout 10s bash -c 'echo -e "bttest\nexit" | make qemu' | grep -A 10 "backtrace:"

echo "测试完成！"
