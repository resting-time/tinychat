#include<sys/socket.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<unistd.h>
#include<fcntl.h>
#include<cstring>
#include<cstdio>
#include<cerrno>
#include<csignal>
#include<ctime>
#include<atomic>
#include<unordered_map>
#include<mutex>
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
std::atomic<uint32_t> g_current_uid{0}; //当前在线uid

const int MAX_EVENTS=64;
const int BUF_SIZE=1024;

//fd->uid
std::unordered_map<int,uint32_t> g_fd2uid;
//uif->fd
std::unordered_map<uint32_t,int> g_uid2fd;
std::mutex g_user_mtx;


inline void send_fail(int fd,uint32_t resp_msg_id){
    tc::Wrapper w;
    w.set_msg_id(resp_msg_id);
    // 所有 resp 里第一个字段都是 bool ok 或 uint32 gid，0/false 表示失败
    std::string empty; // 空包即可
    w.set_payload(empty);
    send_msg(fd,w);
}


void signal_handler(int sig){
    g_running=false;
    printf("\n[%s] caught,exiting...\n",sig==SIGINT?"SIGINT":"SIGTERM");
    exit(0);        //立即退出，不再等epoll
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
                             {
                             std::lock_guard<std::mutex> lk(g_user_mtx);
                             auto it = g_fd2uid.find(fd);
                             if(it != g_fd2uid.end()){
                             g_uid2fd.erase(it->second);
                             g_fd2uid.erase(it);
                             }
                             }
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

                                 {
                                     std::lock_guard<std::mutex> lk(g_user_mtx);
                                     g_fd2uid[fd] = uid;
                                     g_uid2fd[uid] = fd;
                                 }
                                 // 把离线消息一次性推完
                                 auto offline = pull_offline(g_dbpool, uid);
                                 for(auto &bin : offline){
                                     tc::Wrapper w;
                                     w.set_msg_id(4);
                                     w.set_payload(bin);
                                     send_msg(fd, w);
                                 }
                                 return;
                             }

                             //2.心跳
                             if(w.msg_id()==5){
                                 tc::HeartBeat hb;
                                 hb.ParseFromString(w.payload());
                                 g_current_uid=hb.uid();
                                 std::string key="online:"+std::to_string(hb.uid());
                                 g_redis.setex(key,30,"1");      //续期30s
                                 return;                         //心跳不回报
                             }

                             // 3. 聊天消息
                             if(w.msg_id()==3){
                                 tc::ChatReq req;
                                 req.ParseFromString(w.payload());
                                 uint64_t msg_id = next_msg_id(g_dbpool);
                                 if(msg_id==0){ std::cerr<<"next_msg_id failed\n"; return;}

                                 tc::ChatResp resp;
                                 resp.set_msg_id(msg_id);
                                 resp.set_from_uid(g_fd2uid[fd]);          // 心跳里已保证有值
                                 resp.set_content(req.content());

                                 if(req.gid()==0){
                                     // 私聊
                                     resp.set_to_uid(req.to_uid());
                                     auto it = g_uid2fd.find(req.to_uid());
                                     if(it != g_uid2fd.end()){
                                         // 在线直接转发
                                         tc::Wrapper w2; w2.set_msg_id(4); w2.set_payload(resp.SerializeAsString());
                                         send_msg(it->second, w2);
                                     }else{
                                         // 离线存储
                                         save_offline(g_dbpool,msg_id,g_fd2uid[fd],req.to_uid(),req.content(),false);
                                     }
                                 }else{
                                     // 群聊
                                     resp.set_gid(req.gid());
                                     auto members = group_members(g_dbpool,req.gid());
                                     for(uint32_t uid : members){
                                         if(uid == g_fd2uid[fd]) continue; // 不发自己
                                         auto it = g_uid2fd.find(uid);
                                         if(it != g_uid2fd.end()){
                                             tc::Wrapper w2; w2.set_msg_id(4); w2.set_payload(resp.SerializeAsString());
                                             send_msg(it->second, w2);
                                         }else{
                                             save_offline(g_dbpool,msg_id,g_fd2uid[fd],uid,req.content(),true);
                                         }
                                     }
                                 }
                                 return;
                             }
                             // 8. 建群
                             if(w.msg_id()==8){
                                 printf("[SRV] create_group recv\n");
                                 tc::CreateGroupReq req;
                                 req.ParseFromString(w.payload());
                                 auto conn=g_dbpool.borrow();
                                 if(!conn){ send_fail(fd,9); return;}
                                 try{
                                     // 插入群
                                     auto stmt=conn->prepareStatement("INSERT INTO `group`(name,owner_uid) VALUES (?,?)");
                                     // 强制唯一：原名字 + 时间戳后缀
                                     std::string name = req.name() + "_" + std::to_string(time(nullptr));
                                     stmt->setString(1,name);
                                     stmt->setInt(2,g_fd2uid[fd]);
                                     stmt->executeUpdate();
                                     // 拿自增 gid
                                     auto rs=conn->prepareStatement("SELECT LAST_INSERT_ID()")->executeQuery();
                                     rs->next();
                                     uint32_t gid=rs->getUInt(1);
                                     // 把创建者拉进群
                                     auto stmt2=conn->prepareStatement("INSERT INTO group_member(gid,uid) VALUES (?,?)");
                                     stmt2->setInt(1,gid);
                                     stmt2->setInt(2,g_fd2uid[fd]);
                                     stmt2->executeUpdate();
                                     g_dbpool.give_back(std::move(conn));

                                     tc::CreateGroupResp resp;
                                     resp.set_gid(gid);
                                     tc::Wrapper reply;
                                     reply.set_msg_id(9);
                                     reply.set_payload(resp.SerializeAsString());
                                     send_msg(fd,reply);
                                 }catch(sql::SQLException &e){
                                     fprintf(stderr,"[DB] create_group error: %s\n",e.what());
                                     g_dbpool.give_back(std::move(conn));
                                     send_fail(fd,9);
                                 }
                                 return;
                             }

                             // 10. 加群
                             if(w.msg_id()==10){
                                 printf("[SRV] join_group recv\n");
                                 tc::JoinGroupReq req;
                                 req.ParseFromString(w.payload());
                                 auto conn=g_dbpool.borrow();
                                 if(!conn){ send_fail(fd,11); return;}
                                 try{
                                     auto stmt=conn->prepareStatement("INSERT IGNORE INTO group_member(gid,uid) VALUES (?,?)");
                                     stmt->setInt(1,req.gid());
                                     stmt->setInt(2,g_fd2uid[fd]);
                                     stmt->executeUpdate();
                                     g_dbpool.give_back(std::move(conn));

                                     tc::JoinGroupResp resp;
                                     resp.set_ok(true);
                                     tc::Wrapper reply;
                                     reply.set_msg_id(11);
                                     reply.set_payload(resp.SerializeAsString());
                                     send_msg(fd,reply);
                                 }catch(sql::SQLException &e){
                                     fprintf(stderr,"[DB] join_group error: %s\n",e.what());
                                     g_dbpool.give_back(std::move(conn));
                                     send_fail(fd,11);
                                 }
                                 return;
                             }

                             // 12. 退群
                             if(w.msg_id()==12){
                                 tc::LeaveGroupReq req;
                                 req.ParseFromString(w.payload());
                                 auto conn=g_dbpool.borrow();
                                 if(!conn){ send_fail(fd,13); return;}
                                 try{
                                     auto stmt=conn->prepareStatement("DELETE FROM group_member WHERE gid=? AND uid=?");
                                     stmt->setInt(1,req.gid());
                                     stmt->setInt(2,g_fd2uid[fd]);
                                     stmt->executeUpdate();
                                     g_dbpool.give_back(std::move(conn));

                                     tc::LeaveGroupResp resp;
                                     resp.set_ok(true);
                                     tc::Wrapper reply;
                                     reply.set_msg_id(13);
                                     reply.set_payload(resp.SerializeAsString());
                                     send_msg(fd,reply);
                                 }catch(sql::SQLException &e){
                                     fprintf(stderr,"[DB] leave_group error: %s\n",e.what());
                                     g_dbpool.give_back(std::move(conn));
                                     send_fail(fd,13);
                                 }
                                 return;
                             }

                             // 14. 列群
                             if(w.msg_id()==14){
                                 auto conn=g_dbpool.borrow();
                                 if(!conn){ send_fail(fd,14); return;}
                                 auto stmt=conn->prepareStatement("SELECT gid FROM group_member WHERE uid=?");
                                 stmt->setInt(1,g_fd2uid[fd]);
                                 auto rs=stmt->executeQuery();
                                 tc::ListGroupResp resp;
                                 while(rs->next()) resp.add_gids(rs->getUInt(1));
                                 g_dbpool.give_back(std::move(conn));

                                 tc::Wrapper reply;
                                 reply.set_msg_id(14);
                                 reply.set_payload(resp.SerializeAsString());
                                 send_msg(fd,reply);
                                 return;
                             }

                             // 15. 公开群列表
                             if(w.msg_id()==15){
                                 auto conn=g_dbpool.borrow();
                                 if(!conn){ send_fail(fd,16); return;}
                                 auto stmt=conn->prepareStatement("SELECT gid,name FROM `group` ORDER BY gid");
                                 auto rs=stmt->executeQuery();
                                 tc::ListAllGroupsResp resp;
                                 while(rs->next()){
                                     resp.add_gid(rs->getUInt("gid"));
                                     resp.add_name(rs->getString("name"));
                                 }
                                 g_dbpool.give_back(std::move(conn));

                                 tc::Wrapper reply;
                                 reply.set_msg_id(16);
                                 reply.set_payload(resp.SerializeAsString());
                                 send_msg(fd,reply);
                                 return;
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
