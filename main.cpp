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
ThreadPool g_tp;

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
            if(events[i].data.fd==listen_fd){
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
                }
            }else{
                int fd=events[i].data.fd;
                g_tp.enqueue([fd,epfd](){
                             
                char buf[BUF_SIZE];
                while(true){
                    ssize_t n=read(fd,buf,sizeof(buf));
                    if(n>0){
                        write(fd,buf,n);
                    }else if(n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){
                        close(fd);
                        break;
                    }else{
                        break;
                    }
                }
            });
    
        }
        }
    }

    close(listen_fd);
    close(epfd);
    printf("TinyChat server exit gracefully.\n");

    return 0;
}
