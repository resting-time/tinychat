#pragma once
#include"db_pool.h"
#include<cppconn/prepared_statement.h>
#include<cppconn/resultset.h>


inline uint32_t db_register(DbPool &pool,const std::string &name,const std::string &pwd_hash){
    auto conn=pool.borrow();
    if(!conn) return 0;
    try{
        //INSERT 返回自增id
        auto pstmt=conn->prepareStatement("INSERT INTO users(name,pwd_hash) VALUES(?,?)");
        pstmt->setString(1,name);
        pstmt->setString(2,pwd_hash);
        pstmt->executeUpdate();
        //拿last_insert_id
        auto rs=conn->prepareStatement("SELECT LAST_INSERT_ID()")->executeQuery();
        rs->next();
        uint32_t uid=rs->getUInt64(1);
        pool.give_back(std::move(conn));
        fprintf(stderr, "[DB] register name=%s hash=%s → uid=%u\n", name.c_str(), pwd_hash.c_str(), uid);
        return uid;
    }catch(sql::SQLException &e){
        //唯一键冲突 ——>用户名已存在
        pool.give_back(std::move(conn));
        return 0;
    }
}

inline uint32_t db_login(DbPool &pool,const std::string &name,const std::string &pwd_hash){
    auto conn=pool.borrow();
    if(!conn) return 0;
    try{
        auto pstmt=conn->prepareStatement("SELECT id FROM users WHERE name = ? AND pwd_hash = ?");
        pstmt->setString(1,name);
        pstmt->setString(2,pwd_hash);
        auto rs=pstmt->executeQuery();
        if(rs->next()){
            uint32_t uid=rs->getUInt(1);
            pool.give_back(std::move(conn));
            return uid;
        }
        pool.give_back(std::move(conn));
        return 0;       //用户名或密码错误
    }catch(sql::SQLException &e){
        pool.give_back(std::move(conn));
        return 0;
    }
}





