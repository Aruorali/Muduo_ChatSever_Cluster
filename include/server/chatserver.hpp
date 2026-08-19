#ifndef CHATSERVER_HPP
#define CHATSERVER_HPP

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <string>

using namespace muduo;
using namespace muduo::net;

// 服务器主类
class ChatServer
{
public:
    ChatServer(EventLoop *loop, const InetAddress &listenAddr, std::string nameArg);
    void start();
    void onConnection(const TcpConnectionPtr &conn);                           // 连接回调函数
    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time); // 消息回调函数

private:
    TcpServer _server;
    EventLoop *_loop;
};

#endif // CHATSERVER_HPP