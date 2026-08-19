#include "usermodle.hpp"
#include <muduo/base/Logging.h>
#include <cstring>

UserModle::UserModle()
{
    _mysql.connect();
}

bool UserModle::login(int id, std::string password)
{
    MYSQL *conn = _mysql.getConnection();

    // 验证id、密码，且状态必须为offline（防止重复登录）
    {
        MYSQL_STMT *stmt = mysql_stmt_init(conn);
        if (!stmt) return false;

        const char *query = "SELECT id FROM user WHERE id = ? AND password = ? AND state = 'offline'";
        mysql_stmt_prepare(stmt, query, strlen(query));

        MYSQL_BIND bind[2];
        memset(bind, 0, sizeof(bind));

        bind[0].buffer_type = MYSQL_TYPE_LONG;
        bind[0].buffer = &id;

        unsigned long pwdLen = password.length();
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = const_cast<char *>(password.c_str());
        bind[1].buffer_length = pwdLen;
        bind[1].length = &pwdLen;

        if (mysql_stmt_bind_param(stmt, bind)
            || mysql_stmt_execute(stmt))
        {
            LOG_ERROR << "login execute: " << mysql_stmt_error(stmt);
            mysql_stmt_free_result(stmt);
            mysql_stmt_close(stmt);
            return false;
        }

        mysql_stmt_store_result(stmt);
        bool ok = (mysql_stmt_num_rows(stmt) > 0);
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);

        if (!ok) return false;
    }

    // 登录成功，更新状态为online
    User user;
    user.setId(id);
    user.setState("online");
    updateState(user);
    return true;
}

// 用户注册
bool UserModle::reg(User &user)
{
    MYSQL *conn = _mysql.getConnection();

    // 检查用户是否已存在
    {
        MYSQL_STMT *stmt = mysql_stmt_init(conn);
        if (!stmt) return false;

        const char *query = "SELECT id FROM user WHERE name = ?";
        mysql_stmt_prepare(stmt, query, strlen(query));

        MYSQL_BIND bind[1];
        memset(bind, 0, sizeof(bind));
        unsigned long nameLen = user.getName().length();
        bind[0].buffer_type = MYSQL_TYPE_STRING;
        bind[0].buffer = const_cast<char *>(user.getName().c_str());
        bind[0].buffer_length = nameLen;
        bind[0].length = &nameLen;

        mysql_stmt_bind_param(stmt, bind);
        mysql_stmt_execute(stmt);
        mysql_stmt_store_result(stmt);

        bool exists = (mysql_stmt_num_rows(stmt) > 0);

        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);

        if (exists)
            return false;
    }

    // 插入新用户
    {
        MYSQL_STMT *stmt = mysql_stmt_init(conn);
        if (!stmt) return false;

        const char *query = "INSERT INTO user(name, password) VALUES(?, ?)";
        mysql_stmt_prepare(stmt, query, strlen(query));

        MYSQL_BIND bind[2];
        memset(bind, 0, sizeof(bind));

        unsigned long nameLen = user.getName().length();
        bind[0].buffer_type = MYSQL_TYPE_STRING;
        bind[0].buffer = const_cast<char *>(user.getName().c_str());
        bind[0].buffer_length = nameLen;
        bind[0].length = &nameLen;

        unsigned long pwdLen = user.getPassword().length();
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = const_cast<char *>(user.getPassword().c_str());
        bind[1].buffer_length = pwdLen;
        bind[1].length = &pwdLen;

        if (mysql_stmt_bind_param(stmt, bind)
            || mysql_stmt_execute(stmt))
        {
            mysql_stmt_close(stmt);
            return false;
        }

        user.setId(static_cast<int>(mysql_stmt_insert_id(stmt)));
        mysql_stmt_close(stmt);
        return true;
    }
}

// 更新用户的状态信息
void UserModle::updateState(User user)
{
    MYSQL *conn = _mysql.getConnection();
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) return;

    const char *query = "UPDATE user SET state = ? WHERE id = ?";
    mysql_stmt_prepare(stmt, query, strlen(query));

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    unsigned long stateLen = user.getState().length();
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char *>(user.getState().c_str());
    bind[0].buffer_length = stateLen;
    bind[0].length = &stateLen;

    int id = user.getId();
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &id;

    if (mysql_stmt_bind_param(stmt, bind)
        || mysql_stmt_execute(stmt))
    {
        LOG_ERROR << "updateState failed: " << mysql_stmt_error(stmt);
    }
    mysql_stmt_close(stmt);
}

// 重置用户的状态信息（防止上次异常退出后，用户卡在 online 状态无法再登录）
void UserModle::resetState()
{
    _mysql.update("UPDATE user SET state = 'offline' WHERE state = 'online'");
}