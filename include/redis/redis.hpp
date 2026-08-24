#ifndef REDIS_HPP
#define REDIS_HPP

#include<hiredis/hiredis.h>
#include<thread>
#include<functional>
#include<string>

class Redis
{
public:
    Redis();
    ~Redis();

    //连接redis
    bool connect();
    //订阅channel（按用户ID订阅频道）
    bool subscribe(int channel);
    //取消订阅channel
    bool unsubscribe(int channel);

    //发布消息到指定频道（跨服务器消息转发）
    // 参数说明：
    //   channel - 目标用户ID（作为频道名，目标用户所在服务器会订阅此频道）
    //   fromid  - 发送者用户ID
    //   toid    - 接收者用户ID
    //   msg     - 消息内容
    // 返回值：true-发布成功 false-发布失败
    bool publish(int channel, int fromid, int toid, const std::string& msg);

    //在独立线程中接受订阅消息
    void acceptNotifyMsg();

    //设置回调函数（收到跨服务器消息时调用）
    // 回调参数：
    //   channel - 频道ID（即目标用户ID）
    //   fromid  - 发送者用户ID
    //   toid    - 接收者用户ID
    //   msg     - 消息内容
    void setNotifyMsgHandler(std::function<void(int, int, int, std::string)> handler);

private:
    redisContext* _subscribeCtx;  // 订阅专用连接
    redisContext* _publishCtx;    // 发布专用连接

    //回调函数，收到订阅的消息给service处理
    std::function<void(int, int, int, std::string)> _notifyMsgHandler;
};

#endif // REDIS_HPP