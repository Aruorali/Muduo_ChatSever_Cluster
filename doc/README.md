# Muduo ChatServer Cluster - 分布式聊天服务器集群

## 📋 项目概述

基于 **Muduo 网络库** 构建的高性能分布式聊天服务器集群系统，支持用户管理、即时通讯、群组聊天等核心功能。

### ✨ 核心特性

- 🔐 **用户系统**：注册、登录、状态管理
- 💬 **即时消息**：一对一私聊、离线消息存储
- 👥 **社交功能**：好友添加、好友列表
- 🏢 **群组功能**：创建群组、加入群组、群聊消息
- 🌐 **分布式架构**：Redis 跨服务器消息转发
- ⚡ **高性能**：Muduo Reactor 模型 + 多线程 IO
- ⚖️ **负载均衡**：Nginx TCP 四层代理（水平扩展）
- 📊 **可观测性**：实时监控面板 + 性能报告生成

## 🛠️ 技术栈

| 技术 | 版本 | 用途 |
|------|------|------|
| **C++** | 17 | 编程语言 |
| **Muduo** | Latest | 高性能网络库（Reactor 模型） |
| **MySQL** | 5.7+ | 数据持久化（关系型数据库） |
| **Redis** | 6.0+ | 消息中间件（发布订阅） |
| **Nginx** | 1.18+ | 负载均衡（TCP 四层代理） |
| **nlohmann/json** | 3.x | JSON 数据处理 |
| **hiredis** | Latest | Redis C 客户端 |
| **CMake** | 4.0+ | 构建工具 |

## 📁 项目结构

```
Muduo_ChatSever_Cluster/
├── bin/                          # 编译输出目录
│   └── ChatServer               # 可执行文件
├── build/                        # CMake 构建目录
├── include/                      # 头文件目录
│   ├── database/                 # 数据库相关头文件
│   │   ├── db.hpp               # MySQL 连接封装
│   │   ├── user.hpp             # 用户实体类
│   │   ├── usermodle.hpp        # 用户模型
│   │   ├── friendmodel.hpp      # 好友模型
│   │   ├── groupmodel.hpp       # 群组模型
│   │   ├── groupuser.hpp        # 群组成员类
│   │   ├── offlinemsgmodel.hpp  # 离线消息模型
│   │   └── group.hpp            # 群组实体类
│   ├── public/                   # 公共头文件
│   │   └── msgtype.hpp          # 消息类型枚举
│   ├── redis/                    # Redis 相关头文件
│   │   └── redis.hpp            # Redis 客户端封装
│   └── server/                   # 服务器核心头文件
│       ├── chatserver.hpp       # 聊天服务器主类
│       └── chatservice.hpp      # 聊天业务服务类
├── src/                          # 源代码目录
│   ├── server/
│   │   ├── main.cpp            # 主程序入口
│   │   ├── database/           # 数据库操作实现
│   │   ├── redis/              # Redis 操作实现
│   │   └── server/             # 服务器核心实现
│   └── client/                  # 测试客户端
├── thirdpart/                    # 第三方库
│   └── json.hpp                # JSON 库（单头文件）
├── deploy/                       # 部署工具目录
│   └── nginx/                   # Nginx 负载均衡相关
│       ├── chat.conf            # Nginx 配置文件（Stream 模块）
│       ├── setup_nginx.sh       # 一键部署脚本
│       └── monitor.sh           # 实时监控脚本
├── doc/                          # 项目文档
│   ├── README.md                # 项目主文档
│   ├── architecture.md          # 架构设计文档
│   ├── api.md                   # API 接口协议规范
│   ├── database.md              # 数据库设计文档
│   └── deployment.md            # 生产环境部署指南
└── CMakeLists.txt               # 主构建脚本
```

## 🚀 快速开始

### 环境依赖

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential cmake libmysqlclient-dev libhiredis-dev

# CentOS/RHEL
sudo yum install gcc-c++ cmake mysql-devel hiredis-devel
```

### 编译构建

```bash
# 克隆项目
git clone <repository-url>
cd Muduo_ChatSever_Cluster

# 创建构建目录
mkdir -p build && cd build

# CMake 配置与编译
cmake ..
make -j$(nproc)

# 运行服务器
cd ../bin
./ChatServer <ip> <port>
```

### 运行示例

```bash
# 启动服务器实例1（监听端口 8000）
./ChatServer 127.0.0.1 8000

# 启动服务器实例2（监听端口 8001）
./ChatServer 127.0.0.1 8001
```

## 📖 文档索引

- [架构设计文档](./architecture.md) - 系统架构与模块设计
- [API 接口文档](./api.md) - 消息协议与接口说明
- [数据库设计](./database.md) - 数据库表结构设计
- [部署指南](./deployment.md) - 生产环境部署方案

## 🎯 功能模块

### 1. 用户管理模块
- ✅ 用户注册（账号密码验证）
- ✅ 用户登录（密码校验 + 状态更新）
- ✅ 在线状态管理（实时维护在线用户映射）

### 2. 消息通信模块
- ✅ 一对一私聊（在线转发 + 离线存储）
- ✅ 离线消息推送（登录时批量发送）
- ✅ 消息格式：JSON 序列化传输

### 3. 社交关系模块
- ✅ 添加好友（双向关系建立）
- ✅ 好友列表查询（含在线状态显示）
- ✅ 好友关系持久化存储

### 4. 群组功能模块
- ✅ 创建群组（群主指定 + 成员初始化）
- ✅ 加入群组（成员申请 + 权限验证）
- ✅ 群聊消息广播（所有成员接收）

### 5. 分布式支持模块
- ✅ Redis 发布订阅（跨服务器消息路由）
- ✅ 频道机制（按用户 ID 分频道）
- ✅ 异步消息通知（独立线程监听）

## 🔧 配置说明

### MySQL 数据库配置

编辑 `src/server/database/db.cpp`：

```cpp
// 数据库连接参数
const char *host = "127.0.0.1";
const char *user = "root";
const char *passwd = "123456";
const char *dbName = "chat";
unsigned int port = 3306;
```

### Redis 配置

Redis 默认连接地址在 `src/server/redis/redis.cpp`：

```cpp
_subscribeCtx = redisConnect("127.0.0.1", 6379);
_publishCtx = redisConnect("127.0.0.1", 6379);
```

## 📊 性能指标

- **并发连接数**：10,000+
- **消息吞吐量**：50,000+ msg/s
- **响应延迟**：< 10ms (P99)
- **线程模型**：1 mainReactor + N subReactor

## 🤝 贡献指南

欢迎提交 Issue 和 Pull Request！

## 📄 许可证

MIT License

---

**作者**: Chenxi  
**最后更新**: 2026-08-24  
**版本**: v1.0.0