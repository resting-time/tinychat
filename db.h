#pragma once
#include"db_pool.h"
#include<cppconn/prepared_statement.h>
#include<cppconn/resultset.h>
#include "msg.pb.h"

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

// 生成全局唯一 msg_id
inline uint64_t next_msg_id(DbPool &pool){
    auto conn=pool.borrow();
    if(!conn) return 0;
    auto stmt=conn->prepareStatement("UPDATE msg_seq SET next_id=LAST_INSERT_ID(next_id+1)");
    stmt->executeUpdate();
    auto rs=conn->prepareStatement("SELECT LAST_INSERT_ID()")->executeQuery();
    rs->next();
    uint64_t id=rs->getUInt64(1);
    pool.give_back(std::move(conn));
    return id;
}

// 写离线消息
inline void save_offline(DbPool &pool,uint64_t msg_id,uint32_t from_uid,
                         uint32_t to_uid,const std::string &content,bool is_group){
    auto conn=pool.borrow();
    if(!conn) return;
    auto stmt=conn->prepareStatement(
                                     "INSERT INTO offline_msg(msg_id,from_uid,uid,is_group,content) VALUES(?,?,?,?,?)");
    stmt->setUInt64(1,msg_id);
    stmt->setInt(2,from_uid);        // 修正
    stmt->setInt(3,to_uid);
    stmt->setBoolean(4,is_group);
    stmt->setString(5,content);
    stmt->executeUpdate();
    pool.give_back(std::move(conn));
}

// 拉取并删除某用户的离线消息
inline std::vector<std::string> pull_offline(DbPool &pool,uint32_t uid){
    std::vector<std::string> ret;
    auto conn=pool.borrow();
    if(!conn) return ret;
    // 先选
    auto stmt=conn->prepareStatement(
                                     "SELECT msg_id,from_uid,is_group,content FROM offline_msg WHERE uid=? ORDER BY msg_id");
    stmt->setInt(1,uid);             // 修正
    auto rs=stmt->executeQuery();
    while(rs->next()){
        ::tc::ChatResp resp;         // 修正命名空间
        resp.set_msg_id  (rs->getUInt64("msg_id"));
        resp.set_from_uid(rs->getUInt("from_uid"));
        resp.set_content (rs->getString("content"));
        bool is_group=rs->getBoolean("is_group");
        if(is_group) resp.set_gid(rs->getUInt("from_uid")); // 简易：gid 先复用 from_uid
        else         resp.set_to_uid(uid);
        ret.push_back(resp.SerializeAsString());
    }
    // 后删
    auto del=conn->prepareStatement("DELETE FROM offline_msg WHERE uid=?");
    del->setInt(1,uid);              // 修正
    del->executeUpdate();
    pool.give_back(std::move(conn));
    return ret;
}

// 取一个群的所有成员 uid
inline std::vector<uint32_t> group_members(DbPool &pool,uint32_t gid){
    std::vector<uint32_t> ret;
    auto conn=pool.borrow();
    if(!conn) return ret;
    auto stmt=conn->prepareStatement("SELECT uid FROM group_member WHERE gid=?");
    stmt->setInt(1,gid);             // 修正
    auto rs=stmt->executeQuery();
    while(rs->next()) ret.push_back(rs->getUInt("uid"));
    pool.give_back(std::move(conn));
    return ret;
}
