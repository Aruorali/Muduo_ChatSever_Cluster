#include "groupmodel.hpp"
#include "db.hpp"
#include <muduo/base/Logging.h>
#include <cstring>

// 创建群组
bool GroupModel::createGroup(Group &group)
{
    // GroupModel 会被多个 reactor 线程并发调用，所以每次调用建立一条独立连接
    MySQL mysql;
    if (!mysql.connect())
    {
        LOG_ERROR << "createGroup: connect failed";
        return false;
    }

    MYSQL *conn = mysql.getConnection();
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    const char *query = "INSERT INTO allgroup(groupname, groupdesc) VALUES(?, ?)";
    if (mysql_stmt_prepare(stmt, query, strlen(query)))
    {
        LOG_ERROR << "createGroup prepare: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    unsigned long nameLen = group.getName().length();
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char *>(group.getName().c_str());
    bind[0].buffer_length = nameLen;
    bind[0].length = &nameLen;

    unsigned long descLen = group.getDesc().length();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char *>(group.getDesc().c_str());
    bind[1].buffer_length = descLen;
    bind[1].length = &descLen;

    if (mysql_stmt_bind_param(stmt, bind) || mysql_stmt_execute(stmt))
    {
        LOG_ERROR << "createGroup execute: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    group.setId(static_cast<int>(mysql_stmt_insert_id(stmt)));
    mysql_stmt_close(stmt);
    return true;
}

// 用户加入群组
void GroupModel::addGroup(int userid, int groupid, const std::string &role)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        LOG_ERROR << "addGroup: connect failed";
        return;
    }

    MYSQL *conn = mysql.getConnection();
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) return;

    const char *query = "INSERT INTO groupuser(groupid, userid, grouprole) VALUES(?, ?, ?)";
    if (mysql_stmt_prepare(stmt, query, strlen(query)))
    {
        LOG_ERROR << "addGroup prepare: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &groupid;

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &userid;

    unsigned long roleLen = role.length();
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = const_cast<char *>(role.c_str());
    bind[2].buffer_length = roleLen;
    bind[2].length = &roleLen;

    if (mysql_stmt_bind_param(stmt, bind) || mysql_stmt_execute(stmt))
    {
        LOG_ERROR << "addGroup execute: " << mysql_stmt_error(stmt);
    }
    mysql_stmt_close(stmt);
}

// 查询用户加入的所有群组
std::vector<Group> GroupModel::queryGroups(int userid)
{
    MySQL mysql;
    std::vector<Group> vec;
    if (!mysql.connect()) return vec;

    std::string sql = "SELECT g.id, g.groupname, g.groupdesc FROM allgroup g "
                      "INNER JOIN groupuser gu ON g.id = gu.groupid "
                      "WHERE gu.userid = " + std::to_string(userid);

    MYSQL_RES *res = mysql.query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            Group group;
            group.setId(std::stoi(row[0]));
            group.setName(row[1] ? row[1] : "");
            group.setDesc(row[2] ? row[2] : "");
            vec.push_back(group);
        }
        mysql_free_result(res);
    }
    return vec;
}

// 查询群组内其他成员（排除自己）
std::vector<GroupUser> GroupModel::queryGroupUsers(int userid, int groupid)
{
    MySQL mysql;
    std::vector<GroupUser> vec;
    if (!mysql.connect()) return vec;

    std::string sql = "SELECT u.id, u.name, u.state, gu.grouprole FROM user u "
                      "INNER JOIN groupuser gu ON u.id = gu.userid "
                      "WHERE gu.groupid = " + std::to_string(groupid) +
                      " AND gu.userid != " + std::to_string(userid);

    MYSQL_RES *res = mysql.query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            GroupUser gu;
            gu.setId(std::stoi(row[0]));
            gu.setName(row[1] ? row[1] : "");
            gu.setState(row[2] ? row[2] : "");
            gu.setRole(row[3] ? row[3] : "");
            vec.push_back(gu);
        }
        mysql_free_result(res);
    }
    return vec;
}
