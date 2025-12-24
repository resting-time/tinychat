#include "codec.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include<unistd.h>
#include<cstdio>
int main(){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{AF_INET, htons(8888), {INADDR_ANY}};
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return 1;

    tc::Wrapper w;
    w.set_msg_id(1);                       // login_req
    tc::LoginReq req; req.set_name("tom");
    w.set_payload(req.SerializeAsString());
    send_msg(fd, w);

    tc::Wrapper rx;
    if (recv_msg(fd, rx) && rx.msg_id() == 2) {   // login_resp
        tc::LoginResp resp;
        resp.ParseFromString(rx.payload());
        printf("login ok, uid=%u\n", resp.uid());
    }
    close(fd);
    return 0;
}
