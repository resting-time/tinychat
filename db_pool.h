#pragma once
#include<cppconn/connection.h>
#include<cppconn/driver.h>
#include<cppconn/exception.h>
#include<cppconn/statement.h>
#include<cppconn/prepared_statement.h>
#include<cppconn/resultset.h>
#include<vector>
#include<mutex>
#include<memory>


class DbPool
{
public:
    explicit DbPool(size_t n=10){
        for(size_t i=0;i<n;++i){
            try{
                sql::Driver *driver=get_driver_instance();
                std::unique_ptr<sql::Connection> conn(driver->connect("tcp://127.0.0.1:3306","root",""));
                conn->setSchema("tinychat");
                pool.push_back(std::move(conn));
            }catch(sql::SQLException &e){
                fprintf(stderr,"DB pool init err: %s\n",e.what());
                exit(1);
            }
        }
    }

    std::unique_ptr<sql::Connection> borrow(){
        std::lock_guard<std::mutex> lk(mtx);
        if(pool.empty()) return nullptr;
        auto conn=std::move(pool.back());
        pool.pop_back();
        return conn;
    }

    void give_back(std::unique_ptr<sql::Connection> conn){
        if(!conn) return;
        std::lock_guard<std::mutex> lk(mtx);
        pool.push_back(std::move(conn));
    }
private:
    std::vector<std::unique_ptr<sql::Connection>> pool;
    std::mutex mtx;

};

