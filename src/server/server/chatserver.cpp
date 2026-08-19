#include "chatserver.hpp"
#include "chatservice.hpp"
#include "json.hpp"
#include <functional>
#include <muduo/base/Logging.h>

using json = nlohmann::json;
using namespace std::placeholders;

ChatServer::ChatServer(EventLoop *loop, const InetAddress &listenAddr, std::string nameArg)
    : _server(loop, listenAddr, nameArg, TcpServer::kReusePort),
      _loop(loop)
{
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));
    _server.setThreadNum(4); // 1个mainReactor + 3个subReactor
}

void ChatServer::start()
{
    _server.start();
}

void ChatServer::onConnection(const TcpConnectionPtr &conn) // 连接回调函数
{
    if (!conn->connected())
    {
        LOG_INFO << conn->peerAddress().toIpPort() << "closed";
        ChatService::instance()->clientClose(conn);
    }
}

void ChatServer::onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time) // 消息回调函数
{
    std::string bufStr = buf->retrieveAllAsString();
    // 解析json数据
    if (!json::accept(bufStr))
    {
        LOG_ERROR << "Invalid JSON received from " << conn->peerAddress().toIpPort();
        conn->shutdown(); // 非法数据，主动关闭这条连接
        return;
    }
    json js = json::parse(bufStr);
    if (!js.contains("msgid"))
    {
        LOG_ERROR << "Message missing msgid field";
        return;
    }

    auto msghandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    msghandler(conn, js, time);
}
