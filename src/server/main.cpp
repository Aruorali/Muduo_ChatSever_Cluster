#include"chatserver.hpp"
#include"chatservice.hpp"
#include<signal.h>

// 信号处理函数必须异步信号安全，这里只置标志位，真正的收尾放到事件循环线程里做
volatile sig_atomic_t g_stop = 0;

void resethandler(int)
{
    g_stop = 1;
}

int main()
{
    signal(SIGINT,  resethandler);
    signal(SIGTERM, resethandler);

    EventLoop loop;
    InetAddress listenAddr("0.0.0.0", 6000);
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