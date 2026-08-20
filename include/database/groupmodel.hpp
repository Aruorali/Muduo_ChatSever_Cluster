#ifndef GROUPMODEL_HPP
#define GROUPMODEL_HPP

#include <vector>
#include <string>
#include "group.hpp"
#include "groupuser.hpp"

// 群组表的数据操作类
class GroupModel
{
public:
    // 创建群组，成功后在 group 里回填自增 id
    bool createGroup(Group &group);
    // 用户加入群组
    void addGroup(int userid, int groupid, const std::string &role);
    // 查询用户加入的所有群组
    std::vector<Group> queryGroups(int userid);
    // 查询群组内其他成员（排除 userid 自己），用于群聊转发
    std::vector<GroupUser> queryGroupUsers(int userid, int groupid);
};

#endif
