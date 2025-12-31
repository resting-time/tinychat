#include "codec.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include<unistd.h>
#include<cstdio>
#include<thread>
#include<chrono>
int main(){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{AF_INET, htons(8888), {INADDR_ANY}};
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        perror("connect");
        return 1;
    }

    /*1.登录请求*/
    tc::Wrapper w;
    w.set_msg_id(1);                       // login_req
    tc::LoginReq req; req.set_name("tom");
    w.set_payload(req.SerializeAsString());
    if(!send_msg(fd, w)){
        close(fd);
        return 1;
    }

    

    /*2。收登录相应*/
    tc::Wrapper rx;
    if (!recv_msg(fd, rx) && rx.msg_id() != 2) {   // login_resp
        close(fd);
        return 1;
    }
        tc::LoginResp resp;
        resp.ParseFromString(rx.payload());
        uint32_t uid=resp.uid();
        printf("login ok, uid=%u\n", uid);


    /*3.30秒心跳线程*/
    std::thread hb([fd,uid]{
        tc::Wrapper w;
        w.set_msg_id(5);        //heartbeat
        tc::HeartBeat hb;
        hb.set_uid(uid);
        w.set_payload(hb.SerializeAsString());
        while(true){
            if(!send_msg(fd,w)) break;
            printf("heartbeat sent,uid=%u\n",uid);
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    });

    /*4.主线程挂起，等心跳*/
    hb.join();
    close(fd);
    return 0;
}
