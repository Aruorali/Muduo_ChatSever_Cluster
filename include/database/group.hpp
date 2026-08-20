#ifndef GROUP_HPP
#define GROUP_HPP

#include <string>

// 群组实体
class Group
{
public:
    Group(int id = -1, std::string name = "", std::string desc = "")
        : _id(id), _name(name), _desc(desc) {}

    int getId() const { return _id; }
    const std::string& getName() const { return _name; }
    const std::string& getDesc() const { return _desc; }

    void setId(int id) { _id = id; }
    void setName(const std::string &name) { _name = name; }
    void setDesc(const std::string &desc) { _desc = desc; }

private:
    int _id;
    std::string _name;
    std::string _desc;
};

#endif
