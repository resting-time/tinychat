#include<sys/socket.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<unistd.h>
#include<fcntl.h>
#include<cstring>
#include<cstdio>
#include<cerrno>
#include<csignal>
#include<atomic>
#include"threadpool.h"
#include"redis_cli.h"
#include"codec.h"
#include"msg.pb.h"
#include"db.h"

using namespace tc;
ThreadPool g_tp;
RedisCli g_redis;
DbPool g_dbpool;        //10 连接，全局

std::atomic<uint32_t> g_uid{1};
std::atomic<bool> g_running{true};

const int MAX_EVENTS=64;
const int BUF_SIZE=1024;


void signal_handler(int sig){
    g_running=false;
    printf("\n[%s] caught,exiting...\n",sig==SIGINT?"SIGINT":"SIGTERM");
}

int set_nonblock(int fd){
    return fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);

}

int main(){
    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if(listen_fd<0){perror("socket");return 1;}

    int opt=1;
    setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    set_nonblock(listen_fd);

    struct sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(8888);
    addr.sin_addr.s_addr=INADDR_ANY;

    if(bind(listen_fd,(struct sockaddr*)&addr,sizeof(addr))<0)
    {perror("bind");return 1;}
    if(listen(listen_fd,5)<0)
    {perror("listen");return 1;}


    int epfd=epoll_create1(0);

    signal(SIGINT,signal_handler);
    signal(SIGTERM,signal_handler);

    struct epoll_event ev{},events[MAX_EVENTS];
    ev.data.fd=listen_fd;
    ev.events=EPOLLIN|EPOLLET;
    epoll_ctl(epfd,EPOLL_CTL_ADD,listen_fd,&ev);

    printf("TinyChat epoll echo server listening on 8888\n");

    while(g_running){
        int nfds=epoll_wait(epfd,events,MAX_EVENTS,1000);
        if(!g_running)break;
        for(int i=0;i<nfds;++i){
            if(events[i].data.fd==listen_fd){       //新连接
                while(true){
                    int conn=accept(listen_fd,nullptr,nullptr);
                    if(conn<0){
                        if(errno==EAGAIN||errno==EWOULDBLOCK)break;
                        perror("accpet");continue;
                    }
                    set_nonblock(conn);
                    ev.data.fd=conn;
                    ev.events=EPOLLIN|EPOLLET;
                    epoll_ctl(epfd,EPOLL_CTL_ADD,conn,&ev);
                    std::string key="online:"+std::to_string(conn);
                    g_redis.setex(key,60,"1");
                }
            }else{          //客户端可读
                int fd=events[i].data.fd;
                g_tp.enqueue([fd](){
                    tc::Wrapper w;
                    if(!recv_msg(fd,w)){
                        close(fd);
                        return;
                    }


                    /*==========消息分发==============*/
                    //0.注册请求
                    if(w.msg_id()==6){
                        tc::RegisterReq req;
                        req.ParseFromString(w.payload());
                        //简单哈希（生产用bcrypt）
                        std::string pwd_hash=std::to_string(std::hash<std::string>{}(req.pwd()));
                        uint32_t uid=db_register(g_dbpool,req.name(),pwd_hash);
                        tc::RegisterResp resp;
                        resp.set_uid(uid);     //0表示失败
                        tc::Wrapper reply;
                        reply.set_msg_id(7);
                        reply.set_payload(resp.SerializeAsString());
                        send_msg(fd,reply);
                        return;
                    }

                    //1.登录请求（带密码）
                    if(w.msg_id()==1){
                        tc::LoginReq req;
                        req.ParseFromString(w.payload());
                        std::string pwd_hash=std::to_string(std::hash<std::string>{}(req.pwd()));
                        uint32_t uid=db_login(g_dbpool,req.name(),pwd_hash);
                        if(uid==0){     //失败
                            tc::LoginResp resp;
                            resp.set_uid(0);
                            tc::Wrapper reply;
                            reply.set_msg_id(2);
                            reply.set_payload(resp.SerializeAsString());
                            send_msg(fd,reply);
                            return;
                        }

                        //成功：写Redis+回包
                        std::string key="online:"+std::to_string(uid);
                        g_redis.setex(key,60,"1");
                        tc::LoginResp resp;
                        resp.set_uid(uid);
                        tc::Wrapper reply;
                        reply.set_msg_id(2);
                        reply.set_payload(resp.SerializeAsString());
                        send_msg(fd,reply);
                        return;
                    }

                    //2.心跳
                    if(w.msg_id()==5){
                        tc::HeartBeat hb;
                        hb.ParseFromString(w.payload());
                        std::string key="online:"+std::to_string(hb.uid());
                        g_redis.setex(key,30,"1");      //续期30s
                        return;                         //心跳不回报
                    }

                    //3.其他消息（原回显）
                    tc::Wrapper reply;
                    reply.set_msg_id(w.msg_id()+1);
                    reply.set_payload(w.payload());
                    send_msg(fd,reply);
            });
    
        }
        }
    }

    close(listen_fd);
    close(epfd);
    printf("TinyChat server exit gracefully.\n");

    return 0;
}
