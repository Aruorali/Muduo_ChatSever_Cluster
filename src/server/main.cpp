#include"chatserver.hpp"
#include"chatservice.hpp"
#include<muduo/base/Logging.h>
#include<signal.h>

// 信号处理函数必须异步信号安全，这里只置标志位，真正的收尾放到事件循环线程里做
volatile sig_atomic_t g_stop = 0;

void resethandler(int)
{
    g_stop = 1;
}

int main(int argc, char* argv[])
{
    signal(SIGINT,  resethandler);
    signal(SIGTERM, resethandler);

    if(argc !=3)
    {
        LOG_ERROR << "Usage: ChatServer <ip> <port>";
    }

    char *p = argv[1];
    uint16_t port = atoi(argv[2]);
    
    EventLoop loop;
    InetAddress listenAddr(p, port);
    ChatServer server(&loop, listenAddr, "ChatServer");
    server.start();

    // 定时轮询退出标志，在事件循环线程里优雅收尾（此时做数据库操作是安全的）
    loop.runEvery(1.0, [&loop] {
        if (g_stop)
        {
            ChatService::instance()->serverClose();
            loop.quit();
        }
    });

    loop.loop();
    return 0;
}