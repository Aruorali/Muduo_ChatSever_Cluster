# 系统架构设计文档

## 🏗️ 整体架构图

```
┌─────────────────────────────────────────────────────────────┐
│                     Client Layer (客户端)                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │  Client1 │  │  Client2 │  │  Client3 │  │  ClientN │    │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘    │
└───────┼─────────────┼─────────────┼─────────────┼───────────┘
        │             │             │             │
        └─────────────┴─────────────┴─────────────┘
                              │
                    TCP Connection (JSON)
                              │
┌─────────────────────────────────────────────────────────────┐
│                  Server Cluster Layer                        │
│                                                             │
│  ┌─────────────────┐    ┌─────────────────┐                │
│  │   ChatServer-1  │    │   ChatServer-2  │   ...          │
│  │  Port: 8000     │    │  Port: 8001     │                │
│  ├─────────────────┤    ├─────────────────┤                │
│  │  EventLoop      │    │  EventLoop      │                │
│  │  TcpServer      │    │  TcpServer      │                │
│  │  ChatService    │    │  ChatService    │                │
│  └───────┬─────────┘    └───────┬─────────┘                │
│          │                      │                          │
└──────────┼──────────────────────┼──────────────────────────┘
           │                      │
           └──────────┬───────────┘
                      │
              Redis Pub/Sub
                      │
┌─────────────────────┴─────────────────────────────────────┐
│                  Data Storage Layer                         │
│                                                             │
│  ┌──────────────┐         ┌──────────────┐                │
│  │    MySQL      │         │    Redis      │                │
│  │  Database     │         │   Server      │                │
│  ├──────────────┤         ├──────────────┤                │
│  │ - users 表    │         │ - 消息频道    │                │
│  │ - friend 表   │         │ - 在线状态    │                │
│  │ - group 表    │         │ - 消息队列    │                │
│  │ - group_user表│         └──────────────┘                │
│  │ - offline_msg │                                       │
│  └──────────────┘                                         │
└───────────────────────────────────────────────────────────┘
```

## 🎯 核心设计模式

### 1. Reactor 网络模型（Muduo）

采用 **One Loop Per Thread + Thread Pool** 架构：

```
                    ┌──────────────┐
                    │ MainReactor  │ ← 接受新连接
                    │  (Base Loop) │
                    └──────┬───────┘
                           │
              accept 连接分配
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
   ┌────┴────┐       ┌────┴────┐       ┌────┴────┐
   │SubReacto│       │SubReacto│       │SubReacto│
   │  r #1   │       │  r #2   │       │  r #3   │
   └────┬────┘       └────┬────┘       └────┬────┘
        │                 │                 │
   IO多路复用         IO多路复用         IO多路复用
        │                 │                 │
   读写事件处理        读写事件处理        读写事件处理
```

**配置参数**：
```cpp
_server.setThreadNum(4); // 1 mainReactor + 3 subReactor
```

**优势**：
- ✅ 避免锁竞争（每个线程独立 EventLoop）
- ✅ 高并发处理能力
- ✅ 负载均衡（连接均匀分布到 subReactor）

### 2. 单例模式（ChatService）

```cpp
class ChatService {
public:
    static ChatService* instance();  // 全局唯一实例
    
private:
    ChatService();                   // 私有构造函数
    static ChatService service;      // 静态实例
};
```

**使用场景**：
- 全局消息处理器注册中心
- 在线用户状态管理
- 业务逻辑统一入口

### 3. 策略模式（消息处理器映射）

```cpp
// 消息处理器注册
_handlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
_handlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
_handlerMap.insert({SEND_MSG, std::bind(&ChatService::onechat, this, _1, _2, _3)});
// ... 更多处理器

// 动态分发
handler getHandler(int msgid);
```

**优势**：
- ✅ 开闭原则：新增消息类型只需注册新处理器
- ✅ 解耦：消息解析与业务逻辑分离
- ✅ 可扩展：支持动态添加消息类型

## 📦 模块详细设计

### 1. ChatServer 模块（网络层）

**职责**：TCP 连接管理、消息接收转发

```cpp
class ChatServer {
public:
    ChatServer(EventLoop *loop, const InetAddress &listenAddr, std::string name);
    
private:
    TcpServer _server;        // Muduo TCP服务器
    EventLoop *_loop;         // 事件循环指针
    
    void onConnection(const TcpConnectionPtr &conn);      // 连接回调
    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time); // 消息回调
};
```

**核心流程**：

```
客户端连接 → onConnection() → 建立TcpConnection
                                    ↓
客户端发送数据 → onMessage() → JSON解析 → 提取msgid
                                        ↓
                            ChatService::getHandler(msgid)
                                        ↓
                            对应业务处理器执行
```

### 2. ChatService 模块（业务层）

**职责**：业务逻辑处理、用户状态管理

```cpp
class ChatService {
private:
    std::unordered_map<int, handler> _handlerMap;        // 消息处理器映射
    std::unordered_map<int, TcpConnectionPtr> _userOnlineMap; // 在线用户映射
    std::mutex _mutex;                                  // 线程安全锁
};
```

**业务功能矩阵**：

| 消息类型 | 处理函数 | 功能说明 |
|---------|---------|---------|
| `LOGIN_MSG` | `login()` | 用户登录验证 |
| `REG_MSG` | `reg()` | 新用户注册 |
| `SEND_MSG` | `onechat()` | 一对一私聊 |
| `ADD_FRIEND_MSG` | `addfriend()` | 添加好友 |
| `CREATE_GROUP_MSG` | `createGroup()` | 创建群组 |
| `ADD_GROUP_MSG` | `addGroup()` | 加入群组 |
| `GROUP_CHAT_MSG` | `groupChat()` | 群聊消息 |

### 3. Redis 模块（分布式层）

**职责**：跨服务器消息路由、发布订阅通信

#### 双连接架构设计

```
┌─────────────────────────────────────────┐
│            Redis Client                 │
│                                         │
│  ┌─────────────┐    ┌─────────────┐    │
│  │_subscribeCtx│    │_publishCtx  │    │
│  │  (订阅连接)  │    │  (发布连接)  │    │
│  └──────┬──────┘    └──────┬──────┘    │
│         │                  │           │
│    订阅频道            发布消息          │
│    接收通知            发送数据          │
│         │                  │           │
└─────────┼──────────────────┼───────────┘
          │                  │
    ┌─────┴─────┐    ┌──────┴──────┐
    │ SUBSCRIBE │    │  PUBLISH    │
    │   命令    │    │    命令     │
    └───────────┘    └─────────────┘
```

**为什么需要双连接？**
- ⚠️ **Redis 协议限制**：订阅状态的连接不能执行其他命令
- ✅ **职责分离**：订阅专用 + 发布专用，避免阻塞
- ⚡ **性能优化**：异步并行处理

**核心方法**：

```cpp
class Redis {
public:
    bool connect();                                          // 建立双连接
    bool subscribe(int channel);                             // 订阅频道
    bool unsubscribe(int channel);                           // 取消订阅
    bool publish(int channel, const std::string& msg);       // 发布消息
    void acceptNotifyMsg();                                  // 独立线程接收消息
    void setNotifyMsgHandler(std::function<void(int,std::string)> handler); // 设置回调
};
```

**消息流转过程**：

```
Server-A 发布消息                    Server-B 接收消息
─────────────────                    ─────────────────
publish(channel, msg)        
        │
        ▼
  _publishCtx ──────────────────► Redis Server
                                      │
                                      ▼
                              频道消息广播
                                      │
                                      ▼
  _subscribeCtx ◄───────────────── redisGetReply()
        │
        ▼
acceptNotifyMsg() [独立线程]
        │
        ▼
_notifyMsgHandler(channel, msg)
        │
        ▼
  ChatService 处理消息
```

### 4. 数据库模块（持久层）

**职责**：数据 CRUD 操作、事务管理

#### MySQL 封装类

```cpp
class MySQL {
public:
    bool connect();              // 建立连接
    bool update(string sql);     // 执行更新
    MYSQL_RES* query(string sql); // 执行查询
    MYSQL* getConnection();      // 获取原始连接
    
private:
    MYSQL *_conn;               // MySQL连接句柄
};
```

#### ORM 模型层

| 模型类 | 数据表 | 职责 |
|--------|--------|------|
| `UserModle` | users | 用户增删改查 |
| `FriendModel` | friend | 好友关系管理 |
| `GroupModel` | group + group_user | 群组管理 |
| `OfflinMsgModel` | offline_msg | 离线消息存储 |

## 🔀 数据流架构

### 登录流程时序图

```
Client                ChatServer           ChatService          MySQL/Redis
  │                       │                    │                    │
  │── LOGIN_MSG ─────────►│                    │                    │
  │                       │── login() ────────►│                    │
  │                       │                    │── 查询用户 ────────►│
  │                       │                    │◀── 返回结果 ────────│
  │                       │                    │                    │
  │                       │                    │── 更新在线状态 ────►│
  │                       │                    │◀── 成功 ────────────│
  │                       │                    │                    │
  │                       │                    │── 查询离线消息 ────►│
  │                       │                    │◀── 返回消息列表 ────│
  │                       │                    │                    │
  │                       │                    │── 删除离线消息 ────►│
  │                       │                    │                    │
  │                       │                    │── 查询好友列表 ────►│
  │                       │                    │◀── 返回好友列表 ────│
  │                       │                    │                    │
  │◀── LOGIN_MSG_ACK ────│◀── 返回响应 ────────│                    │
  │◀── 离线消息推送 ─────│                    │                    │
  │◀── 好友列表推送 ─────│                    │                    │
```

### 私聊消息流程（跨服务器场景）

```
Client-A              Server-A               Redis              Server-B              Client-B
  │                      │                    │                    │                     │
  │── SEND_MSG ─────────►│                    │                    │                     │
  │                      │── onechat() ──────►│                    │                     │
  │                      │                    │                    │                     │
  │                      │  检查目标用户在线状态                     │                     │
  │                      │  发现用户在 Server-B                      │                     │
  │                      │                    │                     │                     │
  │                      │── publish(userId_B, msg) ──────────────►│                    │
  │                      │                    │                     │                     │
  │                      │                    │── PUBLISH ────────►│                     │
  │                      │                    │                     │                     │
  │                      │                    │── 消息广播 ────────┤                     │
  │                      │                    │                     │                     │
  │                      │                    │                     │── redisGetReply()   │
  │                      │                    │                     │                     │
  │                      │                    │                     │── acceptNotifyMsg() │
  │                      │                    │                     │                     │
  │                      │                    │                     │── 转发消息 ─────────►│
  │                      │                    │                     │                     │
  │◀── SEND_MSG_ACK ────│                    │                     │                     │
```

## 🔒 线程安全设计

### 互斥锁保护区域

```cpp
std::mutex _mutex;

// 在线用户映射的线程安全访问
{
    std::lock_guard<std::mutex> lock(_mutex);
    _userOnlineMap.insert({id, conn});      // 写操作
    _userOnlineMap.erase(id);               // 删除操作
}
```

### 线程模型与资源竞争点

| 资源 | 竞争来源 | 保护机制 |
|------|---------|---------|
| `_userOnlineMap` | 多个 IO 线程并发访问 | `std::mutex` |
| MySQL 连接 | 多线程查询 | MySQL 内部线程安全 |
| Redis 发布连接 | 主线程调用 | 无竞争（单线程写入） |
| Redis 订阅连接 | 独立线程读取 | 无竞争（单线程读取） |

## 📈 可扩展性设计

### 水平扩展方案

1. **无状态服务**：ChatServer 实例不保存会话状态
2. **Redis 中间件**：统一的消息路由中心
3. **数据库共享**：所有实例共享同一 MySQL

### 扩展步骤

```bash
# 启动新实例
./ChatServer 0.0.0.0 8002
./ChatServer 0.0.0.0 8003

# 前端负载均衡（Nginx 示例）
upstream chat_backend {
    server 127.0.0.1:8000;
    server 127.0.0.1:8001;
    server 127.0.0.1:8002;
    server 127.0.0.1:8003;
}
```

## 🚨 异常处理机制

### 客户端异常断开

```cpp
void ChatServer::onConnection(const TcpConnectionPtr &conn) {
    if (!conn->connected()) {
        LOG_INFO << conn->peerAddress().toIpPort() << " closed";
        ChatService::instance()->clientClose(conn);  // 清理资源
    }
}
```

### 服务器优雅退出

```cpp
// 信号处理
signal(SIGINT, resethandler);   // Ctrl+C
signal(SIGTERM, resethandler);  // kill命令

// 定时检查退出标志（在主线程事件循环中）
loop.runEvery(1.0, [&loop] {
    if (g_stop) {
        ChatService::instance()->serverClose();  // 清理所有用户状态
        loop.quit();                             // 退出事件循环
    }
});
```

---

**文档版本**: v1.0  
**最后更新**: 2026-08-24  
**作者**: Chenxi