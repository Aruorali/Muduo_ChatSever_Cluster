# 生产环境部署指南

## 🎯 部署架构图

```
                        ┌─────────────────┐
                        │   Client 用户    │
                        │ (Web/Mobile/PC)  │
                        └────────┬────────┘
                                 │
                    HTTP/WebSocket/TCP
                                 │
                        ┌────────┴────────┐
                        │   Load Balancer  │
                        │     (Nginx)      │
                        └────────┬────────┘
                                 │
              ┌──────────────────┼──────────────────┐
              │                  │                  │
         ┌────┴────┐       ┌────┴────┐       ┌────┴────┐
         │ChatServer│       │ChatServer│       │ChatServer│
         │  Node-1  │       │  Node-2  │       │  Node-N  │
         │ :8000    │       │ :8001    │       │ :800N    │
         └────┬─────┘       └────┬─────┘       └────┬─────┘
              │                  │                  │
              └──────────────────┼──────────────────┘
                                 │
                    Redis Cluster (消息队列)
                                 │
              ┌──────────────────┼──────────────────┐
              │                  │                  │
        ┌─────┴─────┐     ┌─────┴─────┐     ┌─────┴─────┐
        │ MySQL     │     │ Redis     │     │ Monitor   │
        │ Master    │     │ Server    │     │ (Prometheus│
        │           │     │           │     │ + Grafana)│
        └───────────┘     └───────────┘     └───────────┘
```

---

## 📋 环境要求

### 硬件配置（单节点最低要求）

| 组件 | CPU | 内存 | 硬盘 | 网络 |
|------|-----|------|------|------|
| **ChatServer** | 4核 | 8GB | 50GB SSD | 千兆网卡 |
| **MySQL** | 8核 | 16GB | 500GB SSD | 千兆网卡 |
| **Redis** | 4核 | 8GB | 100GB SSD | 千兆网卡 |

### 软件依赖

```bash
# 操作系统
Ubuntu 20.04 LTS / CentOS 7+ / Debian 10+

# 基础工具
GCC >= 9.0 (支持 C++17)
CMake >= 3.16
Make >= 4.0

# 第三方库
Muduo Network Library (已编译)
MySQL Client Library >= 5.7
hiredis >= 0.14
OpenSSL (可选，用于加密)

# 运行时依赖
MySQL Server >= 5.7
Redis Server >= 6.0
Nginx >= 1.18 (负载均衡)
```

---

## 🚀 部署步骤

### 第一步：环境准备

#### 1.1 安装系统依赖

```bash
# Ubuntu/Debian
sudo apt-get update && sudo apt-get upgrade -y
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libmysqlclient-dev \
    libhiredis-dev \
    libssl-dev \
    nginx

# CentOS/RHEL
sudo yum update -y
sudo yum groupinstall -y "Development Tools"
sudo yum install -y \
    cmake3 \
    git \
    mysql-devel \
    hiredis-devel \
    openssl-devel \
    nginx
```

#### 1.2 安装 Muduo 库

```bash
# 方法一：从源码编译（推荐）
git clone https://github.com/chenshuo/muduo.git
cd muduo
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make -j$(nproc)
sudo make install
sudo ldconfig

# 方法二：使用包管理器（如果有）
# sudo apt-get install libmuduo-dev
```

### 第二步：数据库部署

#### 2.1 安装 MySQL

```bash
# Ubuntu
sudo apt-get install -y mysql-server

# CentOS
sudo yum install -y mysql-server
sudo systemctl start mysqld
sudo systemctl enable mysqld
```

#### 2.2 初始化数据库

```bash
# 登录 MySQL
mysql -u root -p

-- 执行初始化脚本
SOURCE /path/to/project/doc/init.sql;

-- 创建应用专用账号
CREATE USER 'chat_app'@'%' IDENTIFIED BY 'YourStrongPassword!';
GRANT SELECT, INSERT, UPDATE, DELETE ON chat.* TO 'chat_app'@'%';
FLUSH PRIVILEGES;

-- 验证连接
SHOW DATABASES;
USE chat;
SHOW TABLES;
```

#### 2.3 MySQL 性能优化

编辑 `/etc/mysql/my.cnf`：

```ini
[mysqld]
# 基础配置
max_connections = 2000
innodb_buffer_pool_size = 4G  # 物理内存的 50-70%
innodb_log_file_size = 512M
innodb_flush_log_at_trx_commit = 2  # 平衡性能与安全

# 慢查询日志
slow_query_log = 1
long_query_time = 2
slow_query_log_file = /var/log/mysql/slow.log

# 字符集
character-set-server = utf8mb4
collation-server = utf8mb4_general_ci
```

重启 MySQL：
```bash
sudo systemctl restart mysql
```

### 第三步：Redis 部署

#### 3.1 安装 Redis

```bash
# Ubuntu
sudo apt-get install -y redis-server

# CentOS
sudo yum install -y redis
sudo systemctl start redis
sudo systemctl enable redis
```

#### 3.2 配置 Redis

编辑 `/etc/redis/redis.conf`：

```conf
# 绑定地址（生产环境建议绑定内网IP）
bind 127.0.0.1 192.168.1.100

# 端口
port 6379

# 密码认证（必须设置！）
requirepass YourRedisPassword!

# 持久化策略
save 900 1      # 15分钟内有1个修改则保存
save 300 10     # 5分钟内有10个修改
save 60 10000   # 1分钟内有10000个修改

# 内存策略
maxmemory 4gb
maxmemory-policy allkeys-lru  # 内存满时淘汰最少使用的key

# 日志级别
loglevel notice
logfile /var/log/redis/redis.log
```

重启 Redis：
```bash
sudo systemctl restart redis
```

验证运行状态：
```bash
redis-cli ping
# 应返回: PONG

redis-cli -a YourRedisPassword! info server
```

### 第四步：编译项目

```bash
# 进入项目目录
cd /opt/Muduo_ChatSever_Cluster

# 创建构建目录
mkdir -p build && cd build

# CMake 配置（Debug 模式）
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译（使用多线程加速）
make -j$(nproc)

# 验证编译结果
ls -lh ../bin/ChatServer
```

### 第五步：配置应用参数

#### 5.1 修改数据库连接信息

编辑 `src/server/database/db.cpp`：

```cpp
bool MySQL::connect()
{
    const char *host = "192.168.1.100";  // MySQL 地址
    const char *user = "chat_app";        // 应用账号
    const char *passwd = "YourStrongPassword!";
    const char *dbName = "chat";
    unsigned int port = 3306;
    
    // ... 连接代码
}
```

#### 5.2 修改 Redis 连接信息

编辑 `src/server/redis/redis.cpp`：

```cpp
bool Redis::connect()
{
    // Redis 地址和端口
    _subscribeCtx = redisConnect("192.168.1.100", 6379);
    _publishCtx = redisConnect("192.168.1.100", 6379);
    
    // 设置密码认证
    if ((_subscribeCtx == NULL || _subscribeCtx->err) ||
        (_publishCtx == NULL || _publishCtx->err))
    {
        return false;
    }
    
    redisCommand(_subscribeCtx, "AUTH %s", "YourRedisPassword!");
    redisCommand(_publishCtx, "AUTH %s", "YourRedisPassword!");
    
    return true;
}
```

#### 5.3 重新编译

```bash
cd build
make -j$(nproc)
```

### 第六步：启动服务

#### 6.1 创建系统服务文件

创建 `/etc/systemd/system/chatserver.service`：

```ini
[Unit]
Description=Muduo ChatServer Service
After=network.target mysql.service redis.service
Requires=mysql.service redis.service

[Service]
Type=simple
User=www-data
Group=www-data
WorkingDirectory=/opt/Muduo_ChatSever_Cluster/bin
ExecStart=/opt/Muduo_ChatSever_Cluster/bin/ChatServer 0.0.0.0 8000
Restart=on-failure
RestartSec=5
LimitNOFILE=65535

# 环境变量
Environment="LD_LIBRARY_PATH=/usr/local/lib"

[Install]
WantedBy=multi-user.target
```

#### 6.2 启动多实例

```bash
# 启动实例 1（端口 8000）
sudo cp /etc/systemd/system/chatserver.service /etc/systemd/system/chatserver-8000.service
sudo sed -i 's/ExecStart=.*/ExecStart=\/opt\/Muduo_ChatSever_Cluster\/bin\/ChatServer 0.0.0.0 8000/' /etc/systemd/system/chatserver-8000.service

# 启动实例 2（端口 8001）
sudo cp /etc/systemd/system/chatserver.service /etc/systemd/system/chatserver-8001.service
sudo sed -i 's/ExecStart=.*/ExecStart=\/opt\/Muduo_ChatSever_Cluster\/bin\/ChatServer 0.0.0.0 8001/' /etc/systemd/system/chatserver-8001.service

# 加载并启动服务
sudo systemctl daemon-reload
sudo systemctl enable chatserver-8000 chatserver-8001
sudo systemctl start chatserver-8000 chatserver-8001
```

#### 6.3 验证服务状态

```bash
# 检查服务状态
sudo systemctl status chatserver-8000
sudo systemctl status chatserver-8001

# 查看日志
journalctl -u chatserver-8000 -f
journalctl -u chatserver-8001 -f

# 测试端口监听
netstat -tlnp | grep ChatServer
ss -tlnp | grep 8000
```

---

## ⚖️ 负载均衡配置（Nginx）

### 方式一：自动部署脚本（推荐 ✨）

项目提供了**一键部署工具**，支持自动化安装配置：

#### 📦 部署文件清单

```
deploy/nginx/
├── chat.conf          # Nginx 负载均衡配置文件
├── setup_nginx.sh     # 自动部署脚本（安装+配置+启动）
└── monitor.sh         # 实时监控脚本（连接数/性能/异常检测）
```

#### 🚀 快速部署

```bash
# 进入部署目录
cd /path/to/project/deploy/nginx

# 授予执行权限
chmod +x setup_nginx.sh monitor.sh

# 完整部署（安装 Nginx + 部署配置 + 启动服务 + 健康检查）
sudo ./setup_nginx.sh --full

# 或者分步执行：
sudo ./setup_nginx.sh --install   # 仅安装 Nginx
sudo ./setup_nginx.sh --config    # 仅部署配置文件
```

**部署脚本功能特性：**
- ✅ **自动检测操作系统**（Ubuntu/CentOS/Debian）
- ✅ **智能依赖检查**（确保 Stream 模块可用）
- ✅ **动态配置生成**（自动检测运行中的 ChatServer 端口）
- ✅ **日志轮转配置**（自动管理日志文件大小）
- ✅ **健康检查验证**（测试端口连通性和 HTTP 服务）
- ✅ **一键卸载清理**（`--uninstall` 完全移除）

#### 📊 实时监控

```bash
# 启动实时监控面板（每 5 秒刷新）
./monitor.sh --realtime

# 单次状态快照
./monitor.sh --once

# 生成完整报告（保存到文件）
./monitor.sh --report

# 查看 TOP 10 活跃客户端
./monitor.sh --top10
```

**监控面板展示内容：**
- 🖥️ 后端实例状态（在线/离线、连接数、响应时间）
- 📈 请求分布统计（响应时间分布、HTTP 状态码）
- 🔍 异常检测（错误率告警、进程健康度）
- 👥 活跃客户端排行（TOP 10 IP 及访问频率）

### 方式二：手动配置（高级用户）

#### 配置 TCP 负载均衡

创建 `/etc/nginx/conf.d/chat.conf`：

```nginx
stream {
    upstream chat_backend {
        least_conn;  // 最少连接算法（适合长连接场景）
        
        server 127.0.0.1:8000 weight=5 max_fails=3 fail_timeout=30s;
        server 127.0.0.1:8001 weight=5 max_fails=3 fail_timeout=30s;
        // 可扩展更多节点...
        
        keepalive 32;  // 保持空闲长连接池
        
        connect_timeout 5s;
    }

    server {
        listen 80;
        proxy_pass chat_backend;
        
        proxy_connect_timeout 5s;
        proxy_timeout 86400s;  // 24小时超时（适应聊天长连接）
    }
}

http {
    // 健康检查端点
    server {
        listen 8081;
        location /health {
            return 200 '{"status":"ok"}';
            add_header Content-Type application/json;
        }
        
        // Nginx 状态监控
        location /nginx_status {
            stub_status on;
            access_log off;
            allow 127.0.0.1;
            deny all;
        }
    }
}
```

#### 测试并重载 Nginx
```bash
// 测试配置语法
sudo nginx -t

// 重载配置（不中断服务）
sudo systemctl reload nginx

// 或重启服务
sudo systemctl restart nginx
```

### 🎯 负载均衡策略详解

#### 支持的负载均衡算法

| 算法 | 配置指令 | 适用场景 | 特点 |
|------|---------|---------|------|
| **轮询（默认）** | 无（默认） | 服务器性能相近 | 简单均匀分配 |
| **最少连接** | `least_conn` | **聊天场景推荐✅** | 长连接负载更均衡 |
| **IP 哈希** | `ip_hash` | 会话保持需求 | 同一IP固定到同一后端 |
| **加权轮询** | `weight=N` | 服务器性能不同 | 按权重分配请求 |

#### 关键参数调优说明

```nginx
server 127.0.0.1:8000 
    weight=5              // 权重（默认为1），数值越大分配越多
    max_fails=3           // 最大失败次数，超过标记为不可用
    fail_timeout=30s      // 失败后重试等待时间
    backup                // 备用服务器（仅当主节点全挂时启用）
    down                  // 永久下线（维护模式）
```

**推荐配置（聊天场景）：**
- 使用 `least_conn` 算法（避免某台服务器长连接过多）
- `fail_timeout=30s`（给故障恢复留足时间）
- `proxy_timeout=86400s`（适应聊天应用的长连接特性）

### 🔍 验证负载均衡效果

#### 1. 检查监听端口
```bash
netstat -tlnp | grep :80
// 应显示: tcp  0  0 0.0.0.0:80  LISTEN  <nginx pid>/nginx
```

#### 2. 测试负载分发
```bash
// 多次访问健康检查端点，观察响应头中的上游服务器标识
for i in {1..10}; do
    curl -I http://127.0.0.1:8081/health
    echo "--- 第 $i 次请求 ---"
done
```

#### 3. 监控连接分布
```bash
// 查看 Nginx 日志中的上游转发记录
tail -f /var/log/nginx/access.log | grep "upstream"

// 或使用项目提供的监控脚本
./deploy/nginx/monitor.sh --realtime
```

### ⚡ 性能优化建议

#### Nginx 全局优化
编辑 `/etc/nginx/nginx.conf`：

```nginx
// 工作进程数（通常设置为 CPU 核心数）
worker_processes auto;

// 单进程最大连接数
events {
    worker_connections 65535;
    use epoll;          // Linux 高性能 IO 多路复用
    multi_accept on;    // 一次接受多个连接
}

http {
    // 开启 Gzip 压缩
    gzip on;
    gzip_min_length 1000;
    gzip_types text/plain application/json;

    // 访问日志格式化（包含上游响应时间）
    log_format main '$remote_addr - $remote_user [$time_local] '
                    '"$request" $status $body_bytes_sent '
                    'rt=$request_time uct="$upstream_connect_time" '
                    'uht="$upstream_header_time" urt="$upstream_response_time"';

    access_log /var/log/nginx/access.log main;
}
```

#### 操作系统内核参数优化
编辑 `/etc/sysctl.conf`：

```bash
// 允许更多 TCP 连接
net.core.somaxconn = 65535
net.ipv4.tcp_max_syn_backlog = 65535

// 快速回收 TIME_WAIT 连接
net.ipv4.tw_reuse = 1
net.ipv4.tw_recycle = 1

// 增大文件描述符限制
fs.file-max = 1048576

// 应用配置
sysctl -p
```

#### 文件描述符限制
编辑 `/etc/security/limits.conf`：

```
* soft nofile 65535
* hard nofile 65535
```

---

## 🔒 安全加固

### 防火墙配置

```bash
# 使用 UFW (Ubuntu)
sudo ufw allow 22/tcp          # SSH
sudo ufw allow 80/tcp          # HTTP
sudo ufw allow 443/tcp         # HTTPS
sudo ufw allow from 192.168.1.0/24 to any port 8000:8010  # 内网集群通信
sudo ufw enable

# 使用 firewalld (CentOS)
sudo firewall-cmd --permanent --add-service=ssh
sudo firewall-cmd --permanent --add-service=http
sudo firewall-cmd --permanent --add-service=https
sudo firewall-cmd --permanent --add-rich-rule='rule family="ipv4" source address="192.168.1.0/24" port port="8000-8010" protocol=tcp accept'
sudo firewall-cmd --reload
```

### SSL/TLS 加密（可选）

如果需要端到端加密，可使用 stunnel 或在应用层实现 TLS：

```bash
# 安装 stunnel
sudo apt-get install stunnel4

# 创建证书（自签名或购买）
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
    -keyout /etc/stunnel/key.pem \
    -out /etc/stunnel/cert.pem

# 配置 stunnel
cat > /etc/stunnel/chat.conf << EOF
[chat-server]
accept = 443
connect = 8000
cert = /etc/stunnel/cert.pem
key = /etc/stunnel/key.pem
EOF

# 启动 stunnel
sudo systemctl enable stunnel4
sudo systemctl start stunnel4
```

---

## 📊 监控与运维

### 日志管理

#### 1. 应用日志轮转

创建 `/etc/logrotate.d/chatserver`：

```
/var/log/chat/*.log {
    daily
    missingok
    rotate 30
    compress
    delaycompress
    notifempty
    create 0640 www-data adm
    sharedscripts
    postrotate
        systemctl reload chatserver-8000 chatserver-8001
    endscript
}
```

#### 2. 日志目录结构

```bash
sudo mkdir -p /var/log/chat
sudo chown www-data:www-data /var/log/chat
```

### 性能监控（Prometheus + Grafana）

#### 1. 安装 Prometheus

```bash
# 下载 Prometheus
wget https://github.com/prometheus/prometheus/releases/download/v2.40.0/prometheus-2.40.0.linux-amd64.tar.gz
tar xvf prometheus-*.tar.gz
cd prometheus-*

# 配置监控目标
cat >> prometheus.yml << EOF
  - job_name: 'chatserver'
    static_configs:
      - targets: ['localhost:8000', 'localhost:8001']
EOF

# 启动 Prometheus
./prometheus --config.file=prometheus.yml
```

#### 2. 关键监控指标

| 指标名称 | 说明 | 告警阈值 |
|---------|------|---------|
| `active_connections` | 当前活跃连接数 | > 8000 |
| `messages_per_second` | 消息吞吐量 | < 50000 |
| `response_time_p99` | 99%响应延迟 | > 100ms |
| `error_rate` | 错误率 | > 1% |
| `cpu_usage` | CPU 使用率 | > 80% |
| `memory_usage` | 内存使用率 | > 85% |

### 备份策略

#### 数据库自动备份脚本

创建 `/opt/scripts/backup_mysql.sh`：

```bash
#!/bin/bash
DATE=$(date +%Y%m%d_%H%M%S)
BACKUP_DIR="/data/backup/mysql"
RETENTION_DAYS=7

# 创建备份目录
mkdir -p $BACKUP_DIR

# 全量备份
mysqldump -u chat_app -p'YourStrongPassword!' \
    --single-transaction \
    --routines \
    --triggers \
    --all-databases \
    | gzip > $BACKUP_DIR/chat_full_$DATE.sql.gz

# 删除过期备份
find $BACKUP_DIR -name "*.sql.gz" -mtime +$RETENTION_DAYS -delete

# 记录日志
echo "$(date): Backup completed - chat_full_$DATE.sql.gz" >> /var/log/backup.log
```

添加定时任务：
```bash
crontab -e
# 每天凌晨 3 点执行全量备份
0 3 * * * /opt/scripts/backup_mysql.sh
```

---

## 🔄 升级与回滚

### 滚动升级流程

```bash
# 1. 准备新版本代码
cd /opt
mv Muduo_ChatSever_Cluster Muduo_ChatSever_Cluster_v1.0
git clone <repository> Muduo_ChatSever_Cluster_v1.1

# 2. 编译新版本
cd Muduo_ChatSever_Cluster_v1.1/build
cmake .. && make -j$(nproc)

# 3. 逐个节点升级（灰度发布）
# 先停止 instance-2
sudo systemctl stop chatserver-8001

# 替换二进制文件
cp ../bin/ChatServer /opt/Muduo_ChatSever_Cluster/bin/

# 更新 service 文件指向新版本
sudo sed -i 's|Muduo_ChatSever_Cluster_v1.0|Muduo_ChatSever_Cluster_v1.1|' /etc/systemd/system/chatserver-8001.service

# 启动新版本
sudo systemctl daemon-reload
sudo systemctl start chatserver-8001

# 观察一段时间无异常后，继续升级其他节点...
# sudo systemctl stop chatserver-8000
# ...重复上述步骤
```

### 回滚方案

```bash
# 如果新版本出现问题，快速回滚到旧版本
sudo systemctl stop chatserver-8001
sudo sed -i 's|Muduo_ChatSever_Cluster_v1.1|Muduo_ChatSever_Cluster_v1.0|' /etc/systemd/system/chatserver-8001.service
sudo systemctl daemon-reload
sudo systemctl start chatserver-8001
```

---

## ❗ 故障排查指南

### 常见问题及解决方案

#### 1. 服务启动失败

**症状**：`systemctl status` 显示 `failed`

**排查步骤**：
```bash
# 查看详细日志
journalctl -u chatserver-8000 -n 50

# 检查端口占用
netstat -tlnp | grep 8000

# 检查依赖服务
systemctl status mysql redis
```

**常见原因**：
- MySQL/Redis 未启动
- 端口被占用
- 配置文件错误
- 权限不足

#### 2. 无法连接数据库

**症状**：登录失败、注册失败

**排查命令**：
```bash
# 测试 MySQL 连接
mysql -h 192.168.1.100 -u chat_app -p -e "SELECT 1"

# 检查防火墙
telnet 192.168.1.100 3306

# 查看 MySQL 错误日志
tail -f /var/log/mysql/error.log
```

#### 3. Redis 消息丢失

**症状**：跨服务器消息未送达

**排查步骤**：
```bash
# 检查 Redis 连接数
redis-cli -a YourRedisPassword! info clients

# 查看订阅频道数量
redis-cli -a YourRedisPassword! PUBSUB CHANNELS

# 监控频道消息
redis-cli -a YourRedisPassword! DEBUG OBJECT channel_*
```

#### 4. 性能突然下降

**排查工具**：
```bash
# 系统资源监控
top -H
htop
vmstat 1

# 网络 I/O
iftop
nethogs

# 磁盘 I/O
iostat -x 1
iotop
```

---

## ✅ 部署检查清单

### 部署前检查

- [ ] 所有依赖库已安装（Muduo、MySQL client、hiredis）
- [ ] MySQL 服务正常运行并可远程连接
- [ ] Redis 服务正常运行并设置密码
- [ ] 数据库表结构已初始化
- [ ] 应用配置文件已更新（数据库地址、密码等）
- [ ] 项目编译成功且无警告
- [ ] 防火墙规则已配置
- [ ] DNS 解析正常（如使用域名）

### 部署后验证

- [ ] 所有 ChatServer 实例进程运行正常
- [ ] 端口监听正确（8000、8001...）
- [ ] Nginx 负载均衡配置生效
- [ ] 可以成功注册新用户
- [ ] 可以成功登录已有用户
- [ ] 在线用户列表显示正确
- [ ] 私聊消息发送接收正常
- [ ] 跨服务器消息转发正常
- [ ] 群组功能正常（创建/加入/群聊）
- [ ] 离线消息存储和推送正常
- [ ] 日志输出正常且无大量 ERROR
- [ ] 监控系统数据采集正常

---

## 📞 技术支持

遇到部署问题？

1. 查看本文档的故障排查章节
2. 检查 GitHub Issues 是否有类似问题
3. 提交新的 Issue 并附上：
   - 操作系统和版本
   - 完整的错误日志
   - 相关配置文件（脱敏处理）
   - 已尝试的解决方法

---

**文档版本**: v1.0  
**最后更新**: 2026-08-24  
**运维团队**: DevOps Team