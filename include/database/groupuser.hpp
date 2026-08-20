#ifndef GROUPUSER_HPP
#define GROUPUSER_HPP

#include <string>
#include "user.hpp"

// 群组成员实体：在用户基础上增加群内角色
class GroupUser : public User
{
public:
    GroupUser() : User(), _role("normal") {}

    void setRole(const std::string &role) { _role = role; }
    const std::string& getRole() const { return _role; }

private:
    std::string _role; // creator / normal
};

#endif
