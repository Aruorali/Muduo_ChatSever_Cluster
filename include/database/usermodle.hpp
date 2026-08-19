#ifndef USERMODLE_HPP
#define USERMODLE_HPP

#include "db.hpp"
#include "user.hpp"

class UserModle
{
public:
    UserModle();
    // 用户登录
    bool login(int id, std::string password);
    // 用户注册
    bool reg(User &user);
    // 更新用户的状态信息
    void updateState(User user);
    // 重置用户的状态信息
    void resetState();

private:
    MySQL _mysql;
};

#endif