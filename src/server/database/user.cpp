#include"user.hpp"

User::User(int id, std::string name, std::string password,
         std::string state)
        : _id(id), _name(name), _password(password), _state(state) 
        {}
int User::getId() const { return _id; }
const std::string& User::getName() const { return _name; }
const std::string& User::getPassword() const { return _password; }
const std::string& User::getState() const { return _state; }
void User::setId(int id) { _id = id; }
void User::setName(const std::string &name) { _name = name; }
void User::setPassword(const std::string &password) { _password = password; }
void User::setState(const std::string &state) { _state = state; }