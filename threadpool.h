#pragma once

#include<vector>
#include<queue>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<functional>

class ThreadPool
{
public:
    explicit ThreadPool(size_t n=8):stop(false){
        for(size_t i=0;i<n;++i)
            workers.emplace_back([this]{
                                 while(true){
                                 std::function<void()> task;
                                 {   //临界区只拿任务
                                 std::unique_lock<std::mutex> lock(q_mtx);
                                 cv.wait(lock,[this]{return stop||!tasks.empty(); });
                                 if(stop&&tasks.empty()) return;
                                 task=std::move(tasks.front());
                                 tasks.pop();
                                 }
                                 task();     //任务出锁后执行
                                 }
                                 });
    }
    ~ThreadPool(){
        {   //通知全部线程退出
            std::unique_lock<std::mutex> lock(q_mtx);
            stop=true;
        }
        cv.notify_all();
        for(std::thread&t : workers) t.join();
    }

    template<class F>
        void enqueue(F &&f){
            {
                std::unique_lock<std::mutex>lock(q_mtx);
                tasks.emplace(std::forward<F>(f));
            }
            cv.notify_one();
        }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex q_mtx;
    std::condition_variable cv;
    bool stop;


};

