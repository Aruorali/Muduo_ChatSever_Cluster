#ifndef CHATSERVICE_HPP
#define CHATSERVICE_HPP

#include<muduo/net/TcpConnection.h>
#include"json.hpp"
#include"offlinemsgmodel.hpp"
#include"friendmodel.hpp"
#include<unordered_map>
#include<functional>
#include<mutex>
#include"redis/redis.hpp"

using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;

using handler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp time)>;

//单例模式
class ChatService
{
public:
    static ChatService* instance();

    //处理登录业务
    void login(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //处理注册业务
    void reg(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //处理发送消息（包含跨服务器转发逻辑）
    void onechat(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //处理添加好友业务
    void addfriend(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //处理创建群组业务
    void createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //处理加入群组业务
    void addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //处理群聊业务
    void groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //客户端异常断开
    void clientClose(const TcpConnectionPtr &conn);
    //服务端异常断开
    void serverClose();

    // 处理从其他服务器通过Redis转发的消息（回调函数）
    // 参数：
    //   channel - 频道ID（目标用户ID）
    //   fromid  - 发送者用户ID
    //   toid    - 接收者用户ID
    //   msg     - 消息内容
    void handleRedisMessage(int channel, int fromid, int toid, const std::string& msg);

    handler getHandler(int msgid);

    // 获取Redis实例（用于测试或外部调用）
    Redis& getRedis() { return _redis; }

private:
    ChatService();

    // 初始化Redis连接和消息处理器注册
    void initRedisHandler();

    std::mutex _mutex;

    std::unordered_map<int, handler> _handlerMap;                   // 存储消息id和其对应的业务处理方法
    std::unordered_map<int, TcpConnectionPtr> _userOnlineMap;       // 存储在线用户的id和连接

    Redis _redis;                                                  // Redis客户端实例（用于跨服务器通信）
};

#endif // CHATSERVICE_HPP