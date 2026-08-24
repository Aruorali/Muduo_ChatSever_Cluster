# 数据库设计文档

## 📊 数据库概览

- **数据库类型**: MySQL 5.7+
- **数据库名称**: `chat`
- **字符集**: utf8mb4
- **排序规则**: utf8mb4_general_ci

## 🗂️ ER 图（实体关系图）

```
┌─────────────┐       ┌─────────────────┐       ┌─────────────┐
│    users    │       │     friend      │       │    group    │
├─────────────┤       ├─────────────────┤       ├─────────────┤
│ PK id (INT) │◄──┐   │ PK userid (INT) │   ┌──►│ PK id (INT) │
│ name        │   │   │ PK friendid(INT)│   │   │ groupname   │
│ password    │   └──►│                 │◄──┘   │ desc        ││
│ state       │       └─────────────────┘       └──────┬──────┘
└─────────────┘                                        │
                                                       │
┌─────────────────┐                              ┌─────┴──────────┐
│   offline_msg   │                              │  group_user    │
├─────────────────┤                              ├────────────────┤
│ PK userid (INT) │                              │ PK groupid(INT)│
│ message (TEXT)  │                              │ PK userid (INT)│
│                 │                              │ role (ENUM)    │
└─────────────────┘                              └────────────────┘
```

**关系说明**：
- `users` ↔ `friend`: 多对多自引用关系（用户之间的好友关系）
- `users` ↔ `group_user` ↔ `group`: 多对多关系（用户与群组）
- `users` → `offline_msg`: 一对多关系（用户的离线消息）

---

## 📋 表结构详细设计

### 1. 用户表（users）

存储系统所有注册用户的基本信息。

```sql
CREATE TABLE IF NOT EXISTS users (
    id INT PRIMARY KEY AUTO_INCREMENT COMMENT '用户ID（主键，自增）',
    name VARCHAR(50) NOT NULL UNIQUE COMMENT '用户昵称（唯一）',
    password VARCHAR(100) NOT NULL COMMENT '用户密码（建议MD5/SHA加密存储）',
    state ENUM('online', 'offline') DEFAULT 'offline' COMMENT '用户状态：online在线/offline离线',
    
    INDEX idx_state (state)  -- 状态索引（优化在线用户查询）
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户信息表';
```

#### 字段说明

| 字段 | 类型 | 约束 | 说明 |
|------|------|------|------|
| `id` | INT | PK, AUTO_INCREMENT | 唯一标识符 |
| `name` | VARCHAR(50) | NOT NULL, UNIQUE | 登录名称，不可重复 |
| `password` | VARCHAR(100) | NOT NULL | 密码明文或哈希值 |
| `state` | ENUM | DEFAULT 'offline' | 在线状态标记 |

#### 示例数据

| id | name | password | state |
|----|------|----------|-------|
| 1 | zhangsan | e10adc3949ba59abbe56e057f20f883e | online |
| 2 | lisi | 1234567890abcdef... | offline |
| 3 | wangwu | abcdef1234567890... | online |

---

### 2. 好友关系表（friend）

记录用户之间的双向好友关系。

```sql
CREATE TABLE IF NOT EXISTS friend (
    userid INT NOT NULL COMMENT '用户ID',
    friendid INT NOT NULL COMMENT '好友的用户ID',
    
    PRIMARY KEY (userid, friendid),  -- 联合主键
    FOREIGN KEY (userid) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (friendid) REFERENCES users(id) ON DELETE CASCADE,
    
    INDEX idx_friendid (friendid)  -- 好友ID索引（优化反向查询）
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='好友关系表';
```

#### 字段说明

| 字段 | 类型 | 约束 | 说明 |
|------|------|------|------|
| `userid` | INT | FK, PK | 当前用户 ID |
| `friendid` | INT | FK, PK | 好友的用户 ID |

#### 设计特点

- ✅ **联合主键**: 保证同一对好友关系只存一次
- ✅ **双向外键**: 级联删除，数据一致性
- ✅ **对称性**: A 是 B 的好友 ⇔ B 也是 A 的好友

#### 示例数据

| userid | friendid |
|--------|----------|
| 1 | 2 |  （张三是李四的好友）
| 2 | 1 |  （李四是张三的好友）
| 1 | 3 |  （张三是王五的好友）
| 3 | 1 |  （王五是张三的好友）

---

### 3. 群组表（group）

存储群组的基本信息。

> ⚠️ **注意**: `group` 是 MySQL 保留字，使用时需要用反引号包裹 `` `group` ``

```sql
CREATE TABLE IF NOT EXISTS `group` (
    id INT PRIMARY KEY AUTO_INCREMENT COMMENT '群组ID（主键，自增）',
    groupname VARCHAR(50) NOT NULL UNIQUE COMMENT '群组名称（唯一）',
    description TEXT COMMENT '群组描述/公告',
    createtime DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    
    INDEX idx_groupname (groupname)  -- 群名索引（支持模糊搜索）
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='群组信息表';
```

#### 字段说明

| 字段 | 类型 | 约束 | 说明 |
|------|------|------|------|
| `id` | INT | PK, AUTO_INCREMENT | 群组唯一标识 |
| `groupname` | VARCHAR(50) | NOT NULL, UNIQUE | 群名称 |
| `description` | TEXT | - | 群介绍或公告 |
| `createtime` | DATETIME | DEFAULT CURRENT_TIMESTAMP | 创建时间 |

#### 示例数据

| id | groupname | description | createtime |
|----|-----------|-------------|------------|
| 1 | 技术交流群 | 讨论技术问题 | 2026-08-24 10:00:00 |
| 2 | 生活闲聊 | 日常聊天 | 2026-08-24 11:30:00 |

---

### 4. 群组成员表（group_user）

记录群组成员及其角色。

```sql
CREATE TABLE IF NOT EXISTS group_user (
    groupid INT NOT NULL COMMENT '群组ID',
    userid INT NOT NULL COMMENT '成员用户ID',
    role ENUM('creator', 'admin', 'normal') DEFAULT 'normal' COMMENT '角色：creator创建者/admin管理员/normal普通成员',
    jointime DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '加入时间',
    
    PRIMARY KEY (groupid, userid),  -- 联合主键（一个用户在同一群组只能有一条记录）
    FOREIGN KEY (groupid) REFERENCES `group`(id) ON DELETE CASCADE,
    FOREIGN KEY (userid) REFERENCES users(id) ON DELETE CASCADE,
    
    INDEX idx_userid (userid)  -- 用户ID索引（查询用户加入的群组）
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='群组成员表';
```

#### 字段说明

| 字段 | 类型 | 约束 | 说明 |
|------|------|------|------|
| `groupid` | INT | FK, PK | 所属群组 ID |
| `userid` | INT | FK, PK | 成员用户 ID |
| `role` | ENUM | DEFAULT 'normal' | 成员角色 |
| `jointime` | DATETIME | DEFAULT CURRENT_TIMESTAMP | 加入时间 |

#### 角色权限说明

| 角色 | 权限范围 |
|------|---------|
| `creator` | 群主：解散群组、移除成员、任命管理员 |
| `admin` | 管理员：审核申请、移除普通成员 |
| `normal` | 普通成员：发送消息、查看成员列表 |

#### 示例数据

| groupid | userid | role | jointime |
|---------|--------|------|----------|
| 1 | 1 | creator | 2026-08-24 10:00:00 |
| 1 | 2 | normal | 2026-08-24 10:05:00 |
| 1 | 3 | admin | 2026-08-24 10:10:00 |

---

### 5. 离线消息表（offline_msg）

存储离线用户的未读消息。

```sql
CREATE TABLE IF NOT EXISTS offline_msg (
    userid INT NOT NULL COMMENT '目标用户ID',
    message TEXT NOT NULL COMMENT 'JSON格式的消息内容',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '消息创建时间',
    
    PRIMARY KEY (userid, created_at),  -- 联合主键（支持同一用户多条消息）
    FOREIGN KEY (userid) REFERENCES users(id) ON DELETE CASCADE,
    
    INDEX idx_created_at (created_at)  -- 时间索引（按时间顺序推送）
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='离线消息表';
```

#### 字段说明

| 字段 | 类型 | 约束 | 说明 |
|------|------|------|------|
| `userid` | INT | FK, PK | 接收消息的目标用户 ID |
| `message` | TEXT | NOT NULL | JSON 格式的完整消息体 |
| `created_at` | TIMESTAMP | DEFAULT CURRENT_TIMESTAMP | 消息入队时间 |

#### message 字段格式示例

```json
{
    "msgid": 5,
    "id": 1001,
    "from": "zhangsan",
    "msg": "你好",
    "time": "2026-08-24 15:30:00"
}
```

#### 示例数据

| userid | message | created_at |
|--------|---------|------------|
| 2 | {"msgid":5,"id":1,"msg":"在吗？"} | 2026-08-24 15:30:00 |
| 2 | {"msgid":13,"id":3,"group":1,"msg":"大家好"} | 2026-08-24 16:00:00 |

---

## 🔍 核心查询语句

### 用户相关查询

```sql
-- 1. 用户登录验证
SELECT id, name, state FROM users WHERE id = #{id} AND password = #{password};

-- 2. 注册新用户
INSERT INTO users(name, password) VALUES(#{name}, #{password});

-- 3. 更新用户状态为在线
UPDATE users SET state = 'online' WHERE id = #{id};

-- 4. 更新用户状态为离线
UPDATE users SET state = 'offline' WHERE id = #{id};
```

### 好友相关查询

```sql
-- 5. 查询某用户的所有好友
SELECT u.id, u.name, u.state 
FROM users u 
INNER JOIN friend f ON u.id = f.friendid 
WHERE f.userid = #{userid};

-- 6. 添加好友关系（双向插入）
INSERT INTO friend(userid, friendid) VALUES(#{userid}, #{friendid});
INSERT INTO friend(userid, friendid) VALUES(#{friendid}, #{userid});
```

### 群组相关查询

```sql
-- 7. 创建群组并初始化成员
START TRANSACTION;
INSERT INTO `group`(groupname, description) VALUES(#{groupname}, #{desc});
SET @last_group_id = LAST_INSERT_ID();

INSERT INTO group_user(groupid, userid, role) VALUES
(@last_group_id, #{creator_id}, 'creator');
-- 循环插入其他成员...
COMMIT;

-- 8. 加入已有群组
INSERT INTO group_user(groupid, userid, role) VALUES(#{groupid}, #{userid}, 'normal');

-- 9. 查询群组的所有成员
SELECT u.id, u.name, gu.role 
FROM users u 
INNER JOIN group_user gu ON u.id = gu.userid 
WHERE gu.groupid = #{groupid};

-- 10. 查询用户加入的所有群组
SELECT g.id, g.groupname, g.description 
FROM `group` g 
INNER JOIN group_user gu ON g.id = gu.groupid 
WHERE gu.userid = #{userid};
```

### 离线消息查询

```sql
-- 11. 查询用户的离线消息
SELECT userid, message FROM offline_msg WHERE userid = #{userid} ORDER BY created_at ASC;

-- 12. 删除已推送的离线消息
DELETE FROM offline_msg WHERE userid = #{userid};
```

---

## 📈 性能优化建议

### 索引策略

```sql
-- 1. 复合索引优化（高频查询场景）
ALTER TABLE friend ADD INDEX idx_userid_friendid (userid, friendid);

-- 2. 覆盖索引（避免回表）
ALTER TABLE users ADD INDEX idx_name_state (name, state);

-- 3. 离线消息分区（按用户ID范围分区）
ALTER TABLE offline_msg PARTITION BY RANGE (userid) (
    PARTITION p0 VALUES LESS THAN (100000),
    PARTITION p1 VALUES LESS THAN (200000),
    PARTITION p2 VALUES LESS THAN MAXVALUE
);
```

### 分库分表方案（大规模场景）

```
用户库（按 user_id 取模分片）
├── chat_db_0.users_0 (user_id % 4 = 0)
├── chat_db_0.users_1 (user_id % 4 = 1)
├── chat_db_1.users_2 (user_id % 4 = 2)
└── chat_db_1.users_3 (user_id % 4 = 3)

消息库（按时间范围分表）
├── offline_msg_202608
├── offline_msg_202609
└── offline_msg_202610
```

### 读写分离配置

```ini
# MySQL 主从复制架构
[Master]
- 写操作：INSERT / UPDATE / DELETE
- 实时性强

[Slave-1, Slave-2]
- 读操作：SELECT
- 最终一致性（延迟 < 1s）

# 应用层路由规则
写请求 → Master DB
读请求 → Slave DB（负载均衡）
```

---

## 🔒 安全加固措施

### 1. 密码加密存储

```cpp
// 推荐使用 SHA-256 + 盐值
#include <openssl/sha.h>

std::string hashPassword(const std::string& password, const std::string& salt) {
    std::string input = password + salt;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), 
           input.size(), hash);
    
    // 转换为十六进制字符串
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return oss.str();
}
```

### 2. SQL 注入防护

```cpp
// 使用参数化查询（预编译语句）
MYSQL_STMT* stmt = mysql_stmt_init(_conn);
const char* query = "SELECT * FROM users WHERE id = ? AND password = ?";
mysql_stmt_prepare(stmt, query, strlen(query));

MYSQL_BIND bind[2];
memset(bind, 0, sizeof(bind));

bind[0].buffer_type = MYSQL_TYPE_LONG;
bind[0].buffer = &id;
bind[1].buffer_type = MYSQL_TYPE_VAR_STRING;
bind[1].buffer = (void*)password.c_str();
bind[1].buffer_length = password.length();

mysql_stmt_bind_param(stmt, bind);
mysql_stmt_execute(stmt);
```

### 3. 数据备份策略

```bash
#!/bin/bash
# 全量备份脚本（每日凌晨执行）
DATE=$(date +%Y%m%d_%H%M%S)
mysqldump -u root -p${MYSQL_PASS} chat > /backup/chat_${DATE}.sql

# 保留最近7天的备份
find /backup -name "chat_*.sql" -mtime +7 -delete

# 二进制日志备份（增量备份，每小时）
mysqlbinlog --read-from-remote-server --host=localhost --raw binlog
```

---

## 🛠️ 数据库维护脚本

### 初始化脚本（init.sql）

```sql
-- 创建数据库
CREATE DATABASE IF NOT EXISTS chat DEFAULT CHARSET utf8mb4 COLLATE utf8mb4_general_ci;
USE chat;

-- 创建表结构
SOURCE create_tables.sql;

-- 插入测试数据
INSERT INTO users(name, password) VALUES('admin', md5('admin123'));
INSERT INTO users(name, password) VALUES('test', md5('test123'));

-- 创建只读账号（用于报表查询）
CREATE USER 'chat_readonly'@'%' IDENTIFIED BY 'readonly_pass';
GRANT SELECT ON chat.* TO 'chat_readonly'@'%';
FLUSH PRIVILEGES;
```

### 监控查询（健康检查）

```sql
-- 1. 表空间使用情况
SELECT 
    table_schema AS '数据库',
    table_name AS '表名',
    ROUND(data_length/1024/1024, 2) AS '数据大小(MB)',
    ROUND(index_length/1024/1024, 2) AS '索引大小(MB)',
    table_rows AS '行数'
FROM information_schema.tables 
WHERE table_schema = 'chat'
ORDER BY data_length DESC;

-- 2. 慢查询分析
SELECT 
    query_time,
    lock_time,
    rows_sent,
    rows_examined,
    sql_text
FROM mysql.slow_log
WHERE start_time > DATE_SUB(NOW(), INTERVAL 1 HOUR)
ORDER BY query_time DESC
LIMIT 10;

-- 3. 连接数监控
SHOW STATUS LIKE 'Threads_connected';
SHOW PROCESSLIST;
```

---

## 📚 扩展阅读

- [MySQL 官方文档](https://dev.mysql.com/doc/)
- [MySQL 索引优化指南](https://dev.mysql.com/doc/refman/8.0/en/optimization.html)
- [数据库设计范式理论](https://en.wikipedia.org/wiki/Database_normalization)

---

**文档版本**: v1.0  
**最后更新**: 2026-08-24  
**DBA**: Chenxi