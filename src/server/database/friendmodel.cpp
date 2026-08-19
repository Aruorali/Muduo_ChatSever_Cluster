#include "friendmodel.hpp"
#include "db.hpp"
#include <muduo/base/Logging.h>
#include <cstring>

// 判断用户是否存在
bool FriendModel::exists(int id)
{
    MySQL mysql;
    if (!mysql.connect()) return false;

    MYSQL_RES *res = mysql.query("SELECT id FROM user WHERE id = " + std::to_string(id));
    if (res == nullptr) return false;

    bool ok = (mysql_num_rows(res) > 0);
    mysql_free_result(res);
    return ok;
}

// 添加好友（双向存储）
bool FriendModel::add(int userid, int friendid)
{
    // 校验好友是否存在
    if (!exists(friendid))
    {
        LOG_ERROR << "friend add: friendid " << friendid << " not exist";
        return false;
    }

    // FriendModel 会被多个 reactor 线程并发调用，所以每次调用建立一条独立连接
    MySQL mysql;
    if (!mysql.connect())
    {
        LOG_ERROR << "friend add: connect failed";
        return false;
    }

    MYSQL *conn = mysql.getConnection();
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    const char *query = "INSERT INTO friend(userid, friendid) VALUES(?, ?)";
    if (mysql_stmt_prepare(stmt, query, strlen(query)))
    {
        LOG_ERROR << "friend add prepare: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    bool ok = true;
    // 双向插入：A 加 B，同时 B 的列表里也出现 A
    int pairs[2][2] = {{userid, friendid}, {friendid, userid}};
    for (auto &p : pairs)
    {
        MYSQL_BIND bind[2];
        memset(bind, 0, sizeof(bind));

        bind[0].buffer_type = MYSQL_TYPE_LONG;
        bind[0].buffer = &p[0];

        bind[1].buffer_type = MYSQL_TYPE_LONG;
        bind[1].buffer = &p[1];

        if (mysql_stmt_bind_param(stmt, bind) || mysql_stmt_execute(stmt))
        {
            LOG_ERROR << "friend add execute: " << mysql_stmt_error(stmt);
            ok = false;
        }
    }

    mysql_stmt_close(stmt);
    return ok;
}

// 查询用户的好友列表
std::vector<User> FriendModel::query(int userid)
{
    MySQL mysql;
    std::vector<User> vec;
    if (!mysql.connect()) return vec;

    // 联表查询，返回好友的 id、名字、状态（不含密码）
    MYSQL_RES *res = mysql.query(
        "SELECT u.id, u.name, u.state FROM user u "
        "INNER JOIN friend f ON u.id = f.friendid "
        "WHERE f.userid = " + std::to_string(userid));

    if (res != nullptr)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            User user;
            user.setId(std::stoi(row[0]));
            user.setName(row[1]);
            user.setState(row[2]);
            vec.push_back(user);
        }
        mysql_free_result(res);
    }
    return vec;
}
