#include "codec.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{AF_INET, htons(8888), {INADDR_ANY}};
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        perror("connect"); return 1;
    }

    std::string name, pwd;
    std::cout << "name: "; std::getline(std::cin, name);
    std::cout << "pwd : "; std::getline(std::cin, pwd);

    // 1. 注册
    tc::Wrapper w;
    w.set_msg_id(6);
    tc::RegisterReq req; req.set_name(name); req.set_pwd(pwd);
    w.set_payload(req.SerializeAsString());
    send_msg(fd, w);

    // 2. 收注册响应
    tc::Wrapper rx;
    if (!recv_msg(fd, rx) || rx.msg_id() != 7) { std::cout << "register failed\n"; return 1; }
    tc::RegisterResp resp; resp.ParseFromString(rx.payload());

    uint32_t uid = 0;
    bool registered = false;  // 标记是否注册成功

    if (resp.uid() == 0) {
        // 注册失败 → 登录已存在的账户
        std::cout << "name existed, try login...\n";
        w.set_msg_id(1);
        tc::LoginReq login; login.set_name(name); login.set_pwd(pwd);
        w.set_payload(login.SerializeAsString());
        send_msg(fd, w);

        // 收登录响应
        if (!recv_msg(fd, rx) || rx.msg_id() != 2) { std::cout << "login failed\n"; return 1; }
        tc::LoginResp login_resp; login_resp.ParseFromString(rx.payload());
        if (login_resp.uid() == 0) { std::cout << "pwd error\n"; return 1; }

        uid = login_resp.uid();
        std::cout << "login ok, uid=" << uid << "\n";
        // 注意：这里不需要再次执行登录流程，已经登录成功了
    } else {
        // 注册成功分支
        uid = resp.uid();
        registered = true;
        std::cout << "register ok, uid=" << uid << "\n";

        // 3. 注册成功后登录
        w.set_msg_id(1);
        tc::LoginReq login; login.set_name(name); login.set_pwd(pwd);
        w.set_payload(login.SerializeAsString());
        send_msg(fd, w);

        // 4. 收登录响应
        if (!recv_msg(fd, rx) || rx.msg_id() != 2) { std::cout << "login failed\n"; return 1; }
        tc::LoginResp login_resp; login_resp.ParseFromString(rx.payload());
        if (login_resp.uid() == 0) { std::cout << "pwd error\n"; return 1; }
        std::cout << "login ok, uid=" << login_resp.uid() << "\n";
    }

    // 5. 心跳 + 聊天
    std::thread hb([fd, uid]{
                   tc::Wrapper w;
                   w.set_msg_id(5);
                   tc::HeartBeat hb; hb.set_uid(uid);
                   w.set_payload(hb.SerializeAsString());
                   while (true) {
                   send_msg(fd, w);
                   std::this_thread::sleep_for(std::chrono::seconds(30));
                   }
                   });
    hb.detach();

    // 后台收 ChatResp（非阻塞）
    std::thread echo([fd]{
                     while (true) {
                     tc::Wrapper rx;
                     if (!recv_msg(fd, rx)) break;
                     if (rx.msg_id() == 4) {
                     tc::ChatResp resp;
                     resp.ParseFromString(rx.payload());
                     std::cout << "[chat] " << resp.content() << std::endl;
                     } else if(rx.msg_id()==9){   // create_group_resp
                     tc::CreateGroupResp resp;
                     resp.ParseFromString(rx.payload());
                     if(resp.gid()==0) std::cout<<"create failed\n";
                     else std::cout<<"create ok, gid="<<resp.gid()<<"\n";
                     } else if(rx.msg_id()==11){
                     tc::JoinGroupResp resp;
                     resp.ParseFromString(rx.payload());
                     std::cout<<"join "<<(resp.ok()?"ok":"failed")<<"\n";
                     } else if(rx.msg_id()==13){
                     tc::LeaveGroupResp resp;
                     resp.ParseFromString(rx.payload());
                     std::cout<<"leave "<<(resp.ok()?"ok":"failed")<<"\n";
                     } else if(rx.msg_id()==14){
                         tc::ListGroupResp resp;
                         resp.ParseFromString(rx.payload());
                         std::cout<<"your groups: ";
                         for(auto gid : resp.gids()) std::cout<<gid<<" ";
                         std::cout<<"\n";
                     }else if(rx.msg_id()==16){
                         tc::ListAllGroupsResp resp;
                         resp.ParseFromString(rx.payload());
                         std::cout<<"=== All Groups ===\n";
                         for(int i=0;i<resp.gid_size();++i){
                             std::cout<<"["<<resp.gid(i)<<"] "<<resp.name(i)<<"\n";
                         }
                     }
                     }
    });
    echo.detach();

    std::string line;
    while (std::getline(std::cin, line)) {
        if(line.empty()) continue;

        /* ========== 命令模式 ========== */
        if(line.rfind("/create ",0)==0){
            std::string name=line.substr(8);
            tc::CreateGroupReq req;
            req.set_name(name);
            tc::Wrapper w;
            w.set_msg_id(8);
            w.set_payload(req.SerializeAsString());
            send_msg(fd,w);
            continue;
        }
        if(line.rfind("/join ",0)==0){
            uint32_t gid=std::stoul(line.substr(6));
            tc::JoinGroupReq req;
            req.set_gid(gid);
            tc::Wrapper w;
            w.set_msg_id(10);
            w.set_payload(req.SerializeAsString());
            send_msg(fd,w);
            continue;
        }
        if(line.rfind("/leave ",0)==0){
            uint32_t gid=std::stoul(line.substr(7));
            tc::LeaveGroupReq req;
            req.set_gid(gid);
            tc::Wrapper w;
            w.set_msg_id(12);
            w.set_payload(req.SerializeAsString());
            send_msg(fd,w);
            continue;
        }
        if(line=="/list"){
            tc::Wrapper w;
            w.set_msg_id(14);
            send_msg(fd,w);
            continue;
        }
        if(line=="/groups"){
            tc::Wrapper w;
            w.set_msg_id(15);
            send_msg(fd,w);
            continue;
        }
        if(line[0]=='/'){
            std::cout<<"unknown cmd\n";
            continue;
        }

        /* ========== 聊天模式 ========== */
        tc::Wrapper w;
        w.set_msg_id(3);  // ChatReq
        tc::ChatReq chat;
        chat.set_content(line);
        std::cout << "0-私聊 1-群聊：";
        int mode; std::cin >> mode;
        if(mode==0){
            std::cout << "to_uid: ";
            int to; std::cin >> to;
            chat.set_to_uid(to);
            chat.set_gid(0);
        }else{
            std::cout << "gid: ";
            int g; std::cin >> g;
            chat.set_gid(g);
            chat.set_to_uid(0);
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(),'\n'); // 清掉换行
        w.set_payload(chat.SerializeAsString());
        send_msg(fd, w);
    }
    close(fd);
    return 0;
}
