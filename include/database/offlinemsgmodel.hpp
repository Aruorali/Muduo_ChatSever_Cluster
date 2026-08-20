#ifndef OFFLINEMSGMODEL_HPP
#define OFFLINEMSGMODEL_HPP

#include<vector>
#include<string>
#include<utility>

class OfflinMsgModel
{
public:
    //储存离线消息（带发送者 id）
    void insert(int id, int senderid, const std::string& msg);
    //删除离线消息
    void remove(int id);
    //查询用户的离线消息，返回 (senderid, message) 对
    std::vector<std::pair<int, std::string>> query(int id);
};

#endif
