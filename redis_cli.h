#pragma once
#include<hiredis/hiredis.h>
#include<string>

class RedisCli
{
public:
    RedisCli(){
        ctx=redisConnect("127.0.0.1",6379);
    }
    ~RedisCli(){
        if(ctx){
            redisFree(ctx);
        }
    }

    bool ok() const{
        return ctx&&ctx->err==0;
    }

    void setex(const std::string &key,int sec,const std::string &val){
        redisReply *r=(redisReply*)redisCommand(ctx,"SETEX %s %d %s",key.c_str(),sec,val.c_str());
        if(r){
            freeReplyObject(r);
        }
    }

    void del(const std::string &key){
        redisReply *r=(redisReply*)redisCommand(ctx,"DEL %s",key.c_str());
        if(r){
            freeReplyObject(r);
        }
    }
    
private:
    redisContext *ctx;
};

