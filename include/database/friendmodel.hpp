#ifndef FRIENDMODEL_HPP
#define FRIENDMODEL_HPP

#include <vector>
#include "user.hpp"

// 好友表的数据操作类
class FriendModel
{
public:
    // 添加好友（双向存储），friendid 不存在或添加失败时返回 false
    bool add(int userid, int friendid);
    // 查询用户的好友列表
    std::vector<User> query(int userid);

private:
    // 判断用户是否存在
    bool exists(int id);
};

#endif
