#include"db.hpp"
#include<muduo/base/Logging.h>

std::string host = "localhost";
std::string user = "root";
std::string password = "123456";
std::string database = "chat";

MySQL::MySQL()
{
    _conn = nullptr;
}

MySQL::~MySQL()
{
    if(_conn != nullptr)
    {
        mysql_close(_conn);
    }
}

bool MySQL::connect()
{
    _conn = mysql_init(nullptr);
    if(_conn == nullptr)
    {
        LOG_ERROR << "MySQL init failed!";
        return false;
    }

    //连接数据库
    MYSQL *p = mysql_real_connect(_conn, host.c_str(), user.c_str(), password.c_str(), database.c_str(), 3306, nullptr, 0);
    if(p == nullptr)
    {
        LOG_ERROR << "MySQL connect failed!";
        return false;
    }
    return true;
}

bool MySQL::update(std::string sql)
{
    if(mysql_query(_conn, sql.c_str()))
    {
        LOG_ERROR << __FILE__ << ":" << __LINE__ << " " << sql << " failed!";
        return false;
    }
    return true;
}

MYSQL_RES* MySQL::query(std::string sql)
{
    if(mysql_query(_conn, sql.c_str()))
    {
        LOG_ERROR << __FILE__ << ":" << __LINE__ << " " << sql << " failed!";
        return nullptr;
    }
    return mysql_store_result(_conn);
}

MYSQL* MySQL::getConnection()
{
    return _conn;
}