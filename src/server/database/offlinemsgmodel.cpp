#include "offlinemsgmodel.hpp"
#include "db.hpp"
#include <muduo/base/Logging.h>
#include <cstring>

// 储存离线消息
void OfflinMsgModel::insert(int id, int senderid, const std::string& msg)
{
    // OfflinMsgModel 是 ChatService 单例的成员，会被多个 reactor 线程并发调用，
    // 所以不能存一个共享的 MySQL 连接，改为每次调用建立一条独立连接
    MySQL mysql;
    if (!mysql.connect())
    {
        LOG_ERROR << "offlinemsg insert: connect failed";
        return;
    }

    MYSQL *conn = mysql.getConnection();
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) return;

    const char *query = "INSERT INTO offlinemessage(userid, senderid, message) VALUES(?, ?, ?)";
    if (mysql_stmt_prepare(stmt, query, strlen(query)))
    {
        LOG_ERROR << "offlinemsg insert prepare: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &id;

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &senderid;

    unsigned long msgLen = msg.length();
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = const_cast<char *>(msg.c_str());
    bind[2].buffer_length = msgLen;
    bind[2].length = &msgLen;

    if (mysql_stmt_bind_param(stmt, bind) || mysql_stmt_execute(stmt))
    {
        LOG_ERROR << "offlinemsg insert execute: " << mysql_stmt_error(stmt);
    }

    mysql_stmt_close(stmt);
}

// 删除离线消息
void OfflinMsgModel::remove(int id)
{
    MySQL mysql;
    if (!mysql.connect()) return;

    mysql.update("DELETE FROM offlinemessage WHERE userid = " + std::to_string(id));
}

// 查询用户的离线消息
std::vector<std::pair<int, std::string>> OfflinMsgModel::query(int id)
{
    MySQL mysql;
    std::vector<std::pair<int, std::string>> vec;
    if (!mysql.connect()) return vec;

    MYSQL_RES *res = mysql.query("SELECT senderid, message FROM offlinemessage WHERE userid = " + std::to_string(id));
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            int senderid = (row[0] != nullptr) ? std::stoi(row[0]) : 0;
            vec.push_back({senderid, row[1] != nullptr ? row[1] : ""});
        }
        mysql_free_result(res);
    }
    return vec;
}
