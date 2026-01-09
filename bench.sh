#!/bin/bash
# usage: ./bench.sh [并发数]   默认 1000
CONC=${1:-1000}                  # 命令行参数 or 默认 1000
SERVER=127.0.0.1
PORT=8888
NAME_PREFIX="bench_user_"

echo "bench start,并发=$CONC"
for i in $(seq 1 $CONC); do
{
    # 每进程：注册→登录→发 10 条消息→退出
    name=$NAME_PREFIX$i
    pwd=$i
    # 注册
    echo -e "$name\n$pwd\nhello from $i\nexit" | timeout 30s ./cli > /dev/null &
} done
wait
echo "bench done"
