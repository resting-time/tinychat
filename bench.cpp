#include "codec.h"
#include "msg.pb.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
using namespace std;
atomic<int> ok{0}, fail{0};
void worker(int wid){
    for(int i=0;i<200;i++){              //每线程200账号
        int fd=socket(AF_INET,SOCK_STREAM,0);
        sockaddr_in addr{AF_INET,htons(8888)};
        inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
        if(connect(fd,(sockaddr*)&addr,sizeof(addr))<0){fail++;close(fd);continue;}

        string name="u"+to_string(wid)+"_"+to_string(i);
        tc::Wrapper w;
        w.set_msg_id(6);
        tc::RegisterReq r; r.set_name(name); r.set_pwd("123");
        w.set_payload(r.SerializeAsString());
        send_msg(fd,w);

        tc::Wrapper rx;
        recv_msg(fd,rx);          //只注册不登录，简化
        ok++;
        close(fd);
    }
}
int main(){
    auto t0=chrono::steady_clock::now();
    vector<thread> tv;
    for(int i=0;i<20;i++) tv.emplace_back(worker,i); //20线程
    for(auto &t:tv) t.join();
    double t=chrono::duration<double>(chrono::steady_clock::now()-t0).count();
    printf("注册成功 %d  失败 %d  耗时 %.1f s  TPS %.0f\n",ok.load(),fail.load(),t,ok.load()/t);
}
