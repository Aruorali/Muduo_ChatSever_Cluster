#include"chatservice.hpp"
#include"msgtype.hpp"
#include"user.hpp"
#include"usermodle.hpp"
#include<muduo/base/Logging.h>

using namespace std::placeholders;

ChatService* ChatService::instance()
{
    static ChatService service;
    return &service;
}

ChatService::ChatService()
{
    _handlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _handlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _handlerMap.insert({SEND_MSG,std::bind(&ChatService::onechat, this, _1, _2, _3)});
}

//获取消息对应的处理器
handler ChatService::getHandler(int msgid)
{
    auto it = _handlerMap.find(msgid);
    if(it == _handlerMap.end())
    {
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp time)
        {
            LOG_ERROR<<"msgid:"<<msgid<<" can not find handler!";
        };
    }
    else
    {
        return it->second;
    }
}

//处理登录业务
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    std::string password = js["password"].get<std::string>();

    UserModle userModle;
    if (userModle.login(id, password))
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _userOnlineMap.insert({id, conn});
        }

        // 登录成功后，向该用户推送离线消息
        std::vector<std::string> offlineMsgs = _offlinemsgmodel.query(id);
        if (!offlineMsgs.empty())
        {
            json chatMsg;
            chatMsg["msgid"] = SEND_MSG;
            for (const std::string &msg : offlineMsgs)
            {
                chatMsg["msg"] = msg;
                conn->send(chatMsg.dump());
            }
            // 推送完毕后删除该用户的离线消息
            _offlinemsgmodel.remove(id);
        }

        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 0;
        conn->send(response.dump());
        LOG_INFO << "user id:" << id << " login success";
    }
    else
    {
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "login failed";
        conn->send(response.dump());
        LOG_ERROR << "user id:" << id << " login failed";
    }
}
//处理注册业务
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    LOG_INFO<<"do reg service";
    User user;
    user.setName(js["name"].get<std::string>());
    user.setPassword(js["password"].get<std::string>());
    UserModle userModle;
    bool state = userModle.reg(user);
    if(state)
    {
        //注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
        LOG_INFO<<"user name:"<<user.getName()<<" reg success!";
    }
    else
    {
        //注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
        LOG_ERROR<<"user name:"<<user.getName()<<" reg failed!";
    }
}
//处理发送消息
void ChatService::onechat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int toid=js["toid"].get<int>();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _userOnlineMap.find(toid);
        if(it!=_userOnlineMap.end())
        {
            //发送消息对象online
            it->second->send(js.dump());
            return;
        }
    }
    //不在线,储存离线消息
    _offlinemsgmodel.insert(toid,js["msg"].get<std::string>());

}
//异常断开
void ChatService::clientClose(const TcpConnectionPtr &conn)
{
    int userId = -1;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto &pair : _userOnlineMap)
        {
            if (pair.second == conn)
            {
                userId = pair.first;
                break;
            }
        }
        if (userId != -1)
            _userOnlineMap.erase(userId);
    }

    if (userId != -1)
    {
        User user;
        user.setId(userId);
        user.setState("offline");
        UserModle userModle;
        userModle.updateState(user);
        LOG_INFO << "user id:" << userId << " disconnected, state set to offline";
    }
}