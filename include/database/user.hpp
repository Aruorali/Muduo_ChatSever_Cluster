#ifndef USER_HPP
#define USER_HPP

#include <string>

class User
{
public:
    User(int id = -1, std::string name = "", std::string password = "",
         std::string state = "offline");
    int getId() const;
    const std::string& getName() const;
    const std::string& getPassword() const;
    const std::string& getState() const;
    void setId(int id);
    void setName(const std::string &name);
    void setPassword(const std::string &password);
    void setState(const std::string &state);
    
private:
    int _id;
    std::string _name;
    std::string _password;
    std::string _state; // 用户状态：online、offline
};

#endif