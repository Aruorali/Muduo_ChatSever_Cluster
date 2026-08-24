# API 接口文档 - 消息协议规范

## 📨 通信协议概述

### 协议格式
- **传输层**: TCP
- **应用层**: JSON (UTF-8 编码)
- **消息分隔**: TCP 包边界（Muduo 自动处理）

### 基本消息结构

```json
{
    "msgid": <int>,        // 消息类型ID（必填）
    ...                    // 其他字段根据消息类型而定
}
```

## 🔢 消息类型枚举

| 枚举值 | 常量名 | 类型 | 说明 |
|--------|--------|------|------|
| 1 | `LOGIN_MSG` | Request | 用户登录请求 |
| 2 | `LOGIN_MSG_ACK` | Response | 登录响应 |
| 3 | `REG_MSG` | Request | 用户注册请求 |
| 4 | `REG_MSG_ACK` | Response | 注册响应 |
| 5 | `SEND_MSG` | Request | 发送私聊消息 |
| 6 | `ADD_FRIEND_MSG` | Request | 添加好友请求 |
| 7 | `ADD_FRIEND_ACK` | Response | 添加好友响应 |
| 8 | `FRIEND_LIST_MSG` | Response | 好友列表推送 |
| 9 | `CREATE_GROUP_MSG` | Request | 创建群组请求 |
| 10 | `CREATE_GROUP_ACK` | Response | 创建群组响应 |
| 11 | `ADD_GROUP_MSG` | Request | 加入群组请求 |
| 12 | `ADD_GROUP_ACK` | Response | 加入群组响应 |
| 13 | `GROUP_CHAT_MSG` | Request | 群聊消息 |

---

## 📋 接口详细说明

### 1. 用户登录接口

#### 请求消息（LOGIN_MSG）

**msgid**: `1`

**请求参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `id` | int | ✅ | 用户 ID |
| `password` | string | ✅ | 用户密码 |

**请求示例**：
```json
{
    "msgid": 1,
    "id": 1001,
    "password": "123456"
}
```

#### 响应消息（LOGIN_MSG_ACK）

**msgid**: `2`

**响应参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `errno` | int | ✅ | 错误码（0=成功，非0=失败） |
| `errmsg` | string | ❌ | 错误信息（失败时返回） |

**成功响应示例**：
```json
{
    "msgid": 2,
    "errno": 0
}
```

**失败响应示例**：
```json
{
    "msgid": 2,
    "errno": 1,
    "errmsg": "用户名或密码错误"
}
```

**附加推送消息**：
- 登录成功后会自动推送：
  - 📬 **离线消息**（msgid=5）
  - 👥 **好友列表**（msgid=8）

---

### 2. 用户注册接口

#### 请求消息（REG_MSG）

**msgid**: `3`

**请求参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `name` | string | ✅ | 用户昵称 |
| `password` | string | ✅ | 用户密码 |

**请求示例**：
```json
{
    "msgid": 3,
    "name": "zhangsan",
    "password": "123456"
}
```

#### 响应消息（REG_MSG_ACK）

**msgid**: `4`

**响应参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `errno` | int | ✅ | 错误码（0=成功） |
| `id` | int | ❌ | 新用户 ID（成功时返回） |

**成功响应示例**：
```json
{
    "msgid": 4,
    "errno": 0,
    "id": 15
}
```

**失败响应示例**：
```json
{
    "msgid": 4,
    "errno": 1,
    "errmsg": "注册失败"
}
```

---

### 3. 私聊消息接口

#### 请求消息（SEND_MSG）

**msgid**: `5`

**请求参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `id` | int | ✅ | 发送者用户 ID |
| `from` | string | ✅ | 发送者名称 |
| `to` | int | ✅ | 接收者用户 ID |
| `msg` | string | ✅ | 消息内容 |
| `time` | string | ⚠️ | 发送时间（可选） |

**请求示例**：
```json
{
    "msgid": 5,
    "id": 1001,
    "from": "zhangsan",
    "to": 1002,
    "msg": "你好，最近怎么样？",
    "time": "2026-08-24 15:30:00"
}
```

**处理逻辑**：
1. 检查接收者是否在线
2. **在线**：直接转发给接收者的 TcpConnection
3. **离线**：存储到离线消息表 + 通过 Redis 跨服务器转发
4. 返回发送确认给发送者

**接收端收到消息格式**（由服务器主动推送）：
```json
{
    "msgid": 5,
    "id": 1001,
    "msg": "你好，最近怎么样？"
}
```

---

### 4. 添加好友接口

#### 请求消息（ADD_FRIEND_MSG）

**msgid**: `6`

**请求参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `id` | int | ✅ | 当前用户 ID |
| `friendid` | int | ✅ | 要添加的好友 ID |

**请求示例**：
```json
{
    "msgid": 6,
    "id": 1001,
    "friendid": 1002
}
```

#### 响应消息（ADD_FRIEND_ACK）

**msgid**: `7`

**响应参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `errno` | int | ✅ | 错误码（0=成功） |

**成功响应示例**：
```json
{
    "msgid": 7,
    "errno": 0
}
```

---

### 5. 好友列表推送

**msgid**: `8` （仅服务端推送）

**触发时机**：用户登录成功后自动推送

**推送数据结构**：

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `msgid` | int | 固定值 8 |
| `friends` | array | 好友列表数组 |

**friends 数组元素**：

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `id` | int | 好友用户 ID |
| `name` | string | 好友昵称 |
| `state` | string | 在线状态（"online"/"offline"） |

**推送示例**：
```json
{
    "msgid": 8,
    "friends": [
        {
            "id": 1002,
            "name": "lisi",
            "state": "online"
        },
        {
            "id": 1003,
            "name": "wangwu",
            "state": "offline"
        }
    ]
}
```

---

### 6. 创建群组接口

#### 请求消息（CREATE_GROUP_MSG）

**msgid**: `9`

**请求参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `id` | int | ✅ | 创建者（群主）用户 ID |
| `groupname` | string | ✅ | 群组名称 |
| `desc` | string | ❌ | 群组描述 |
| `users` | array | ✅ | 初始成员列表（包含群主） |

**users 数组元素**：

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `id` | int | 成员用户 ID |
| `name` | string | 成员昵称 |

**请求示例**：
```json
{
    "msgid": 9,
    "id": 1001,
    "groupname": "技术交流群",
    "desc": "讨论技术问题",
    "users": [
        {
            "id": 1001,
            "name": "zhangsan"
        },
        {
            "id": 1002,
            "name": "lisi"
        },
        {
            "id": 1003,
            "name": "wangwu"
        }
    ]
}
```

#### 响应消息（CREATE_GROUP_ACK）

**msgid**: `10`

**响应参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `errno` | int | ✅ | 错误码（0=成功） |
| `groupid` | int | ❌ | 新创建的群组 ID |

**成功响应示例**：
```json
{
    "msgid": 10,
    "errno": 0,
    "groupid": 5
}
```

---

### 7. 加入群组接口

#### 请求消息（ADD_GROUP_MSG）

**msgid**: `11`

**请求参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `id` | int | ✅ | 申请加入的用户 ID |
| `groupid` | int | ✅ | 目标群组 ID |

**请求示例**：
```json
{
    "msgid": 11,
    "id": 1004,
    "groupid": 5
}
```

#### 响应消息（ADD_GROUP_ACK）

**msgid**: `12`

**响应参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `errno` | int | ✅ | 错误码（0=成功） |

**成功响应示例**：
```json
{
    "msgid": 12,
    "errno": 0
}
```

---

### 8. 群聊消息接口

#### 请求消息（GROUP_CHAT_MSG）

**msgid**: `13`

**请求参数**：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `id` | int | ✅ | 发送者用户 ID |
| `name` | string | ✅ | 发送者名称 |
| `group` | int | ✅ | 目标群组 ID |
| `msg` | string | ✅ | 消息内容 |
| `time` | string | ⚠️ | 发送时间（可选） |

**请求示例**：
```json
{
    "msgid": 13,
    "id": 1001,
    "name": "zhangsan",
    "group": 5,
    "msg": "大家好！",
    "time": "2026-08-24 16:00:00"
}
```

**处理逻辑**：
1. 验证发送者是否为群组成员
2. 查询该群组的所有成员
3. 对每个成员：
   - **在线且在本服务器**：直接推送
   - **在线但在其他服务器**：通过 Redis 转发
   - **离线**：存储离线消息

**群成员收到的消息格式**（服务器推送）：
```json
{
    "msgid": 13,
    "id": 1001,
    "name": "zhangsan",
    "group": 5,
    "msg": "大家好！",
    "time": "2026-08-24 16:00:00"
}
```

---

## 🔐 错误码定义

| 错误码 | 含义 | 触发场景 |
|--------|------|---------|
| `0` | 成功 | 操作完成 |
| `1` | 一般性错误 | 参数缺失、业务逻辑错误 |
| `2` | 数据库错误 | MySQL 操作失败 |
| `3` | Redis 错误 | Redis 连接/操作失败 |
| `4` | 权限不足 | 非群组成员发群消息等 |
| `5` | 用户不存在 | 目标用户 ID 无效 |
| `6` | 群组不存在 | 目标群组 ID 无效 |

---

## 📊 完整通信流程示例

### 场景：用户 A 登录并给好友 B 发消息

```
步骤 1: 用户A登录
Client-A → Server: {"msgid":1, "id":1001, "password":"123456"}
Server → Client-A: {"msgid":2, "errno":0}  [登录成功]
Server → Client-A: {"msgid":8, "friends":[...]}  [好友列表]
Server → Client-A: {"msgid":5, "id":1003, "msg":"你好"}  [离线消息]

步骤 2: 用户A发送私聊消息
Client-A → Server: {
    "msgid":5,
    "id":1001,
    "from":"zhangsan",
    "to":1002,
    "msg":"在吗？"
}

情况A: 用户B在线（同一服务器）
Server → Client-B: {"msgid":5, "id":1001, "msg":"在吗？"}

情况B: 用户B在线（不同服务器）
Server → Redis: PUBLISH channel_1002 {"msgid":5, "id":1001, "msg":"在吗？"}
Redis → Server-B: 消息通知
Server-B → Client-B: {"msgid":5, "id":1001, "msg":"在吗？"}

情况C: 用户B离线
Server → MySQL: INSERT INTO offline_msg VALUES(1002, "在吗？")
[等待用户B上线时推送]
```

---

## ⚠️ 注意事项

### 1. JSON 格式要求
- 所有字段名必须使用**小写字母**
- 字符串值必须使用**双引号**
- 时间格式建议使用 ISO 8601：`YYYY-MM-DD HH:MM:SS`

### 2. 消息顺序保证
- 同一连接的消息按发送顺序处理
- 跨服务器消息可能存在微小延迟（< 50ms）

### 3. 安全建议
- 生产环境应启用 TLS 加密
- 密码应使用 MD5/SHA 哈希后传输
- 建议添加 Token 认证机制

### 4. 性能优化建议
- 批量发送多条消息时合并为一次 send()
- 心跳包检测连接存活性（建议间隔 30s）
- 大文本消息考虑分片传输

---

## 🧪 测试工具推荐

### 使用 telnet 测试

```bash
telnet 127.0.0.1 8000

# 输入JSON消息（注意换行）
{"msgid":3,"name":"test","password":"123"}
```

### 使用 netcat 测试

```bash
echo '{"msgid":3,"name":"test","password":"123"}' | nc 127.0.0.1 8000
```

### Python 测试脚本

```python
import socket
import json

def send_msg(msg):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('127.0.0.1', 8000))
    
    data = json.dumps(msg).encode('utf-8')
    sock.send(data)
    
    response = sock.recv(4096).decode('utf-8')
    print("Response:", response)
    sock.close()

# 测试注册
send_msg({"msgid": 3, "name": "testuser", "password": "123456"})
```

---

**文档版本**: v1.0  
**最后更新**: 2026-08-24  
**维护者**: Chenxi