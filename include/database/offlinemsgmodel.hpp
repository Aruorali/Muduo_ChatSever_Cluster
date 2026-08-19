#ifndef OFFLINEMSGMODEL_HPP
#define OFFLINEMSGMODEL_HPP

#include<vector>
#include<string>

class OfflinMsgModel
{
public:
    //储存离线消息
    void insert(int id,const std::string& msg);
    //删除离线消息
    void remove(int id);
    //查询用户的离线消息
    std::vector<std::string> query(int id);
};

#endif