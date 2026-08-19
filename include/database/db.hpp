#ifndef DB_HPP
#define DB_HPP

#include<mysql/mysql.h>
#include<string>

class MySQL
{
public:
    MySQL();
    ~MySQL();

    //连接数据库
    bool connect();
    //更新操作
    bool update(std::string sql);
    //查询操作
    MYSQL_RES* query(std::string sql);
    //获取连接
    MYSQL* getConnection();
private:
    MYSQL *_conn;
};

#endif // DB_HPP