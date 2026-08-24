# Redis 跨服务器消息转发 - 完整技术文档

## 🎯 功能概述

实现 **分布式聊天服务器集群** 之间的消息路由功能，确保用户无论连接到哪台服务器，都能实时收发消息。

---

## 🏗️ 架构设计

### 核心思想：**用户频道机制**

```
每个用户拥有一个专属的 Redis 频道
频道名 = 用户ID (channel_{userid})

当用户在 Server-A 登录 → Server-A 订阅 channel_{userid}
当其他服务器要给该用户发消息 → PUBLISH 到 channel_{userid}
Server-A 收到消息 → 转发给在线用户 或 存储离线消息
```

### 完整数据流图

```
场景：用户1（在Server-A）给 用户2（在Server-B）发消息

┌─────────────┐                    ┌─────────────┐
│   User-1    │                    │   User-2    │
│  (Client)   │                    │  (Client)   │
└──────┬──────┘                    └──────┬───────┘
       │                                  │
       │ ① SEND_MSG {from:1, to:2, msg}  │
       ├─────────────────────────────────►│
       │                          ⑤ 转发消息│
       │◄─────────────────────────────────┤
       │                                  │
┌──────┴──────┐                    ┌──────┴───────┐
│  Server-A   │                    │  Server-B    │
│ (发送方)     │                    │ (接收方)      │
│             │                    │              │
│ ② onechat() │                    │ ⑥ handleRedis│
│ 查找toid=2  │                    │ Message()    │
│ 不在本机在线 │                    │              │
│             │                    │ 查找toid=2   │
│ ③ redis.publish(               │ 在本机在线 ✓  │
│   channel=2,                   │              │
│   from=1,to=2,msg)             │ ⑦ 直接转发    │
│             │                    │ 给User-2     │
└──────┬──────┘                    └──────┬───────┘
       │ ④ PUBLISH "1:2:hello"           │
       │         to channel_2            │
       ├────────────────┐                │
       │                │                │
┌──────┴────────┐  ┌────┴───────────┐   │
│   Redis       │  │  offline_msg   │   │
│   Server      │  │  Table (MySQL) │   │
│               │  │                │   │
│ channel_2 订阅者│  │ 如果User-2也   │   │
│ = [Server-B]  │  │ 离线则存储这里  │   │
│               │  │                │   │
└───────────────┘  └────────────────┘   │
```

---

## 📝 代码修改清单

### 1️⃣ Redis 类（redis.hpp / redis.cpp）

#### ✅ 修改前（旧版本）
```cpp
// 头文件声明（2个参数）
bool publish(int channel, const std::string& msg);
void setNotifyMsgHandler(std::function<void(int,std::string)> handler);

// 实现文件（错误的PUBLISH格式）
bool Redis::publish(int channel, const std::string &msg)
{
    reply = redisCommand(_publishCtx, "PUBLISH %d %s", channel, msg.c_str());
}
```

#### ✅ 修改后（新版本）
```cpp
// 头文件声明（4个参数）
bool publish(int channel, int fromid, int toid, const std::string& msg);
void setNotifyMsgHandler(std::function<void(int,int,int,std::string)> handler);

// 实现文件（正确的消息封装）
bool Redis::publish(int channel, int fromid, int toid, const std::string& msg)
{
    // 封装消息体："fromid:toid:msg"
    std::string message = std::to_string(fromid) + ":" + std::to_string(toid) + ":" + msg;

    // 执行Redis命令：PUBLISH <channel> <message>
    reply = redisCommand(_publishCtx, "PUBLISH %d %s", channel, message.c_str());
}

// 接收端解析消息
void Redis::acceptNotifyMsg()
{
    // 收到消息后解析 "fromid:toid:msg" 格式
    std::string message = reply->element[2]->str;
    // 按 ':' 分隔提取 fromid, toid, msg
    _notifyMsgHandler(channel, fromid, toid, msg);
}
```

#### 🔑 关键改进点

| 改进项 | 说明 |
|--------|------|
| **参数扩展** | publish 从2个参数 → 4个参数（增加 fromid/toid） |
| **消息封装** | 使用 `"fromid:toid:msg"` 格式，便于接收端解析 |
| **正确协议** | 修复 `PUBLISH` 命令格式错误（之前是3个参数） |
| **回调增强** | 回调函数从2个参数 → 4个参数 |

---

### 2️⃣ ChatService 类（chatservice.hpp / chatservice.cpp）

#### ✅ 新增成员变量
```cpp
private:
    Redis _redis;  // Redis客户端实例
```

#### ✅ 新增方法
```cpp
public:
    void handleRedisMessage(int channel, int fromid, int toid, const std::string& msg);

private:
    void initRedisHandler();
```

#### ✅ 构造函数更新
```cpp
ChatService::ChatService()
{
    // ... 注册消息处理器 ...

    // 初始化Redis连接和回调
    initRedisHandler();  // 🆕 新增
}
```

#### ✅ login() 更新（用户登录时订阅频道）
```cpp
void ChatService::login(...)
{
    if (userModle.login(id, password))
    {
        // 添加到在线列表
        _userOnlineMap.insert({id, conn});

        // 🆕 订阅用户的Redis频道
        _redis.subscribe(id);  // 关键！
    }
}
```

#### ✅ onechat() 更新（核心转发逻辑）
```cpp
void ChatService::onechat(...)
{
    int fromid = js["id"];
    int toid = js["toid"];
    string msg = js["msg"];

    // 1. 检查目标用户是否在本服务器在线
    {
        auto it = _userOnlineMap.find(toid);
        if (it != _userOnlineMap.end())
        {
            // 在线 → 直接转发（最优路径）
            it->second->send(response.dump());
            return;
        }
    }

    // 2. 不在线 → 通过Redis跨服务器转发
    bool success = _redis.publish(toid, fromid, toid, msg);  // 🆕 核心！

    // 3. 存储离线消息（兜底，防止消息丢失）
    OfflinMsgModel offlinemsgmodel;
    offlinemsgmodel.insert(toid, fromid, msg);
}
```

#### ✅ clientClose() 更新（用户下线时取消订阅）
```cpp
void ChatService::clientClose(...)
{
    // 从在线列表移除
    _userOnlineMap.erase(userId);

    // 🆕 取消订阅Redis频道
    _redis.unsubscribe(userId);

    // 更新状态为离线
    user.setState("offline");
}
```

#### ✅ handleRedisMessage() 新增（处理来自其他服务器的消息）
```cpp
void ChatService::handleRedisMessage(int channel, int fromid, int toid, const string& msg)
{
    // 收到从其他服务器通过Redis转发的消息

    // 检查目标用户是否在本服务器在线
    auto it = _userOnlineMap.find(toid);
    if (it != _userOnlineMap.end())
    {
        // 在线 → 直接转发
        it->second->send(response.dump());
    }
    else
    {
        // 离线 → 存储离线消息
        OfflinMsgModel offlinemsgmodel;
        offlinemsgmodel.insert(toid, fromid, msg);
    }
}
```

---

## 🔄 完整工作流程

### 场景 1：同服务器通信（无需Redis）

```
User-1 ──→ Server-A ──→ User-2
  (都在Server-A)     (直接转发)

流程：
1. User-1 发送消息给 User-2
2. Server-A 的 onechat() 查找 toid=2
3. 发现 User-2 在本机 _userOnlineMap 中
4. 直接调用 conn->send() 转发
5. ✅ 完成！（未使用Redis）
```

### 场景 2：跨服务器通信 - 目标在线（需要Redis）

```
User-1 ──→ Server-A ──[Redis]──→ Server-B ──→ User-2
 (在A)                  (在B)

详细步骤：
① User-1 发送 {from:1, to:2, msg:"hello"}
② Server-A onechat() 查找 toid=2
③ 不在本地在线 → 调用 redis.publish(2, 1, 2, "hello")
④ Redis 内部：
   - 构造消息体："1:2:hello"
   - 查找 channel_2 的订阅者
   - 发现 Server-B 订阅了 channel_2
   - 转发消息给 Server-B
⑤ Server-B acceptNotifyMsg() 收到消息
⑥ 解析出 channel=2, from=1, to=2, msg="hello"
⑦ 调用 handleRedisMessage(2, 1, 2, "hello")
⑧ 查找 toid=2 → 在本机在线 → 直接转发给 User-2
✅ 完成！
```

### 场景 3：跨服务器通信 - 目标离线（Redis + 离线存储）

```
User-1 ──→ Server-A ──[Redis]──→ Server-B ──[MySQL]── (等待上线)
 (在A)                 (离线)

详细步骤：
①~④ 同上（通过Redis发布消息）
⑤ Server-B 收到消息
⑥ handleRedisMessage() 查找 toid=2
⑦ 不在本地在线 → 存储到 offline_msg 表
   INSERT INTO offline_msg VALUES(2, 1, "1:2:hello")
✅ 消息已安全存储！

后续：当 User-2 上线时
⑧ User-2 在 Server-C 登录
⑨ Server-C login() 查询 offline_msg 表
⑩ 推送离线消息给 User-2
⑪ 删除已推送的离线消息
✅ User-2 收到了延迟的消息！
```

---

## ⚙️ 配置与部署

### Redis 配置要求

```conf
# /etc/redis/redis.conf
port 6379
bind 0.0.0.0          # 允许所有服务器连接
requirepass yourpassword  # 设置密码（生产环境必须）
maxmemory 4gb          # 根据用户规模调整
maxmemory-policy allkeys-lru
```

### 多服务器部署拓扑

```
                    ┌─────────────┐
                    │   Redis     │
                    │   Server    │
                    │  (6379)     │
                    └──────┬──────┘
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
   ┌────────────┐   ┌────────────┐   ┌────────────┐
   │ Server-A   │   │ Server-B   │   │ Server-C   │
   │ :8000      │   │ :8001      │   │ :8002      │
   │            │   │            │   │            │
   │ Users:     │   │ Users:     │   │ Users:     │
   │ 1,3,5,7..  │   │ 2,4,6,8..  │   │ 9,10,11..  │
   └────────────┘   └────────────┘   └────────────┘

每个服务器启动时：
1. 连接 Redis（_redis.connect()）
2. 注册消息回调（setNotifyMsgHandler）
3. 启动独立线程监听订阅消息（acceptNotifyMsg）

用户登录时：
1. 验证账号密码
2. 加入 _userOnlineMap
3. 订阅用户频道：SUBSCRIBE {userid}

用户下线时：
1. 从 _userOnlineMap 移除
2. 取消订阅：UNSUBSCRIBE {userid}
3. 更新状态为offline
```

---

## 🔍 测试验证方法

### 单机测试（模拟多服务器）

```bash
# 终端1：启动 Redis
redis-server

# 终端2：启动 Server-A（端口 8000）
./ChatServer 127.0.0.1 8000

# 终端3：启动 Server-B（端口 8001）
./ChatServer 127.0.0.1 8001

# 终端4：连接 Server-A，注册用户1
telnet 127.0.0.1 8000
> {"msgid":3,"name":"user1","pass":"123"}
> {"msgid":1,"id":1,"pass":"123"}  # 登录

# 终端5：连接 Server-B，注册用户2
telnet 127.0.0.1 8001
> {"msgid":3,"name":"user2","pass":"123"}
> {"msgid":1,"id":2,"pass":"123"}  # 登录

# 回到终端4：用户1给用户2发消息
> {"msgid":5,"id":1,"toid":2,"msg":"Hello from Server-A!"}

# 观察终端5：应该收到消息！
# 同时观察日志输出，查看Redis发布/订阅过程
```

### 验证要点

- [ ] Server-A 日志显示：`publish to Redis channel 2`
- [ ] Server-B 日志显示：`Received message from Redis: from=1, to=2`
- [ ] User-2 客户端收到：`{"msgid":5,"id":1,"msg":"Hello from Server-A!"}`
- [ ] Redis 命令行验证：
  ```bash
  redis-cli
  > PUBSUB CHANNELS  # 查看活跃频道
  > PUBSUB NUMSUB channel_2  # 查看channel_2的订阅数（应为1）
  ```

---

## ⚠️ 注意事项与优化建议

### 当前实现的限制

1. **离线消息可能重复存储**
   - 问题：Redis 发布成功 + 本地存储离线消息 → 如果目标服务器在线并转发，消息会立即收到；但离线消息也已存储，下次登录会再次推送
   - 影响：用户可能会收到重复消息
   - 解决方案：修改 `Redis::publish()` 返回订阅数，只在 `subscriber_count == 0` 时存储离线消息

2. **消息顺序性**
   - 当前不保证严格有序（并发场景下）
   - 如需严格有序，可引入 Redis Stream 或消息队列

3. **可靠性保障**
   - Redis 宕机期间的消息会丢失（除非开启 AOF 持久化）
   - 建议生产环境配置 Redis 主从 + Sentinel

### 性能优化建议

```cpp
// 1. 批量订阅（减少Redis连接开销）
// 用户批量登录时，可以一次性订阅多个频道
void batchSubscribe(std::vector<int> userIds);

// 2. 消息压缩（减少网络传输量）
// 对长消息使用 Snappy/LZ4 压缩
std::string compress(const std::string& msg);

// 3. 本地缓存热点频道的订阅状态
// 避免频繁查询 Redis
std::unordered_map<int, bool> _subscribedChannels;

// 4. 连接池复用
// 如果用户量很大，考虑 Redis 连接池
```

---

## 📊 监控指标

建议监控以下关键指标：

| 指标 | 说明 | 告警阈值 |
|------|------|---------|
| Redis 发布 QPS | 每秒发布的消息数 | > 10000/s |
| Redis 订阅延迟 | 消息从发布到接收的延迟 | > 50ms |
| 离线消息积压量 | 未读离线消息数量 | > 10000 条 |
| 频道订阅数 | 当前活跃的用户频道数 | 异常波动 |
| Redis 连接数 | 与Redis的TCP连接数 | > 1000 |

监控命令：
```bash
# Redis 发布/订阅统计
redis-cli INFO stats | grep -E "total_connections_received|instantaneous_ops_per_sec"

# 查看频道信息
redis-cli PUBSUB CHANNELS
redis-cli PUBSUB NUMSUB

# 离线消息数量
mysql -e "SELECT COUNT(*) FROM chat.offline_msg;"
```

---

## 🎯 总结

### 核心价值

✅ **真正的分布式架构**：不再局限于单服务器  
✅ **透明的消息路由**：用户无感知，自动选择最优路径  
✅ **高可用设计**：单点故障不影响整体服务  
✅ **平滑扩容**：新增服务器只需启动并连接 Redis  

### 技术亮点

🔥 **用户频道模型**：每个用户一个频道，精准路由  
🔥 **双连接架构**：订阅/发布分离，避免协议冲突  
🔥 **智能降级策略**：Redis 不可用时退化为本地离线存储  
🔥 **完整生命周期管理**：登录订阅、下线取消订阅  

---

**文档版本**: v1.0  
**最后更新**: 2026-08-24  
**作者**: Chenxi