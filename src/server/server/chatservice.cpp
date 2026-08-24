#include"chatservice.hpp"
#include"msgtype.hpp"
#include"user.hpp"
#include"usermodle.hpp"
#include"friendmodel.hpp"
#include"group.hpp"
#include"groupmodel.hpp"
#include<muduo/base/Logging.h>

using namespace std::placeholders;

ChatService* ChatService::instance()
{
    static ChatService service;
    return &service;
}

ChatService::ChatService()
{
    // 注册消息处理器
    _handlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _handlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _handlerMap.insert({SEND_MSG,std::bind(&ChatService::onechat, this, _1, _2, _3)});
    _handlerMap.insert({ADD_FRIEND_MSG,std::bind(&ChatService::addfriend, this, _1, _2, _3)});
    _handlerMap.insert({CREATE_GROUP_MSG,std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _handlerMap.insert({ADD_GROUP_MSG,std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _handlerMap.insert({GROUP_CHAT_MSG,std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 初始化Redis连接和消息处理回调
    initRedisHandler();
}

// 初始化Redis连接和消息处理器
// 在构造函数中调用，建立与Redis的连接并注册消息接收回调
void ChatService::initRedisHandler()
{
    LOG_INFO << "Initializing Redis connection...";

    // 连接Redis服务器（建立订阅和发布两个连接）
    if (!_redis.connect())
    {
        LOG_ERROR << "Failed to connect to Redis server!";
        LOG_WARN << "System will continue running without Redis support (single-server mode)";
        // 注意：不直接返回或退出，允许系统在无Redis模式下运行（降级模式）
        // 跨服务器消息转发功能将不可用，但本地消息仍可正常工作
        return;
    }

    LOG_INFO << "Successfully connected to Redis server";

    // 设置收到订阅消息时的回调函数
    // 当其他服务器通过Redis发送消息到本服务器时，会调用 handleRedisMessage 处理
    _redis.setNotifyMsgHandler(std::bind(&ChatService::handleRedisMessage, this, _1, _2, _3, _4));

    LOG_INFO << "Redis message handler registered successfully";
}

//获取消息对应的处理器
handler ChatService::getHandler(int msgid)
{
    auto it = _handlerMap.find(msgid);
    if(it == _handlerMap.end())
    {
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp time)
        {
            LOG_ERROR<<"msgid:"<<msgid<<" can not find handler!";
        };
    }
    else
    {
        return it->second;
    }
}

//处理登录业务
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    std::string password = js["password"].get<std::string>();

    UserModle userModle;
    if (userModle.login(id, password))
    {
        // 用户登录成功，添加到在线用户映射
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _userOnlineMap.insert({id, conn});
        }

        // 订阅该用户的Redis频道（用于接收其他服务器转发的消息）
        // 频道ID就是用户ID，这样当其他服务器要给此用户发消息时，
        // 会通过 PUBLISH 命令发送到此频道，本服务器就能收到
        if (!_redis.subscribe(id))
        {
            LOG_ERROR << "Failed to subscribe Redis channel for user " << id;
            // 订阅失败不影响登录，只是无法接收跨服务器消息
        }
        else
        {
            LOG_INFO << "Subscribed Redis channel for user " << id;
        }

        // 登录成功后，向该用户推送离线消息
        OfflinMsgModel _offlinemsgmodel;
        std::vector<std::pair<int, std::string>> offlineMsgs = _offlinemsgmodel.query(id);
        if (!offlineMsgs.empty())
        {
            json chatMsg;
            chatMsg["msgid"] = SEND_MSG;
            for (const auto &p : offlineMsgs)
            {
                chatMsg["id"] = p.first;   // 发送者 id
                chatMsg["msg"] = p.second;
                conn->send(chatMsg.dump());
            }
            // 推送完毕后删除该用户的离线消息
            _offlinemsgmodel.remove(id);
        }

        //登录成功推送好友列表
        FriendModel friendModel;
        std::vector<User> friends = friendModel.query(id);
        if (!friends.empty())
        {
            json friendList;
            friendList["msgid"] = FRIEND_LIST_MSG;
            for (const User &f : friends)
            {
                json friendInfo;
                friendInfo["id"] = f.getId();
                friendInfo["name"] = f.getName();
                friendInfo["state"] = f.getState();
                friendList["friends"].push_back(friendInfo);
            }
            conn->send(friendList.dump());
        }

        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 0;
        conn->send(response.dump());
        LOG_INFO << "user id:" << id << " login success";
    }
    else
    {
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "login failed";
        conn->send(response.dump());
        LOG_ERROR << "user id:" << id << " login failed";
    }
}
//处理注册业务
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    LOG_INFO<<"do reg service";
    User user;
    user.setName(js["name"].get<std::string>());
    user.setPassword(js["password"].get<std::string>());
    UserModle userModle;
    bool state = userModle.reg(user);
    if(state)
    {
        //注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
        LOG_INFO<<"user name:"<<user.getName()<<" reg success!";
    }
    else
    {
        //注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
        LOG_ERROR<<"user name:"<<user.getName()<<" reg failed!";
    }
}
//处理发送消息（包含完整的跨服务器转发逻辑）
//
// 消息处理流程：
// 1. 解析消息：获取接收者ID(toid)、发送者ID(fromid)、消息内容(msg)
// 2. 本地查找：检查目标用户是否在当前服务器在线
//   - 在线 → 直接转发（最优路径，无需经过Redis）
// 3. 跨服务器转发：
//   - 不在本服务器在线 → 通过Redis PUBLISH发布到目标用户的频道
//   - 目标用户所在的服务器订阅了该频道，会收到消息
// 4. 离线存储：
//   - 如果Redis返回0（没有服务器订阅该频道，说明目标用户完全离线）
//   - 将消息存储到离线消息表，等用户上线时推送
void ChatService::onechat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int fromid = js["id"].get<int>();           // 发送者ID
    int toid = js["toid"].get<int>();             // 接收者ID
    std::string msg = js["msg"].get<std::string>(); // 消息内容

    LOG_INFO << "User " << fromid << " send message to user " << toid;

    // 第一步：检查目标用户是否在当前服务器在线
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _userOnlineMap.find(toid);
        if(it != _userOnlineMap.end())
        {
            // 目标用户在本服务器在线，直接转发消息（最快路径）
            LOG_INFO << "User " << toid << " is online on this server, send directly";

            // 构造发送给接收者的JSON消息
            json response;
            response["msgid"] = SEND_MSG;
            response["id"] = fromid;      // 发送者ID
            response["msg"] = msg;         // 消息内容
            it->second->send(response.dump());
            return;
        }
    }

    // 第二步：目标用户不在本服务器在线，尝试通过Redis跨服务器转发
    LOG_INFO << "User " << toid << " is not online on this server, try Redis publish";

    // 通过Redis发布消息到目标用户的频道
    // 参数说明：
    //   channel: toid (目标用户ID作为频道名)
    //   fromid:  发送者ID
    //   toid:    接收者ID
    //   msg:     消息内容
    bool publishSuccess = _redis.publish(toid, fromid, toid, msg);

    if (publishSuccess)
    {
        LOG_INFO << "Message published to Redis channel " << toid;
        // 注意：publish成功只表示Redis收到了消息，不代表目标用户在线
        // 如果没有服务器订阅该频道（reply->integer == 0），
        // 说明目标用户可能在所有服务器都离线，需要存储离线消息

        // 这里可以选择：
        // 方案A：总是存储离线消息（简单但可能重复）
        // 方案B：只在确定无人在线时存储（需要修改Redis::publish返回订阅数）
        // 当前采用方案A，确保消息不丢失
        OfflinMsgModel _offlinemsgmodel;
        _offlinemsgmodel.insert(toid, fromid, msg);
        LOG_INFO << "Message saved to offline storage for user " << toid;
    }
    else
    {
        // Redis发布失败，降级为本地离线存储
        LOG_ERROR << "Failed to publish message via Redis, save to offline storage";

        OfflinMsgModel _offlinemsgmodel;
        _offlinemsgmodel.insert(toid, fromid, msg);
    }

    // 返回发送确认给发送者（可选）
    json ack;
    ack["errno"] = 0;
    ack["toid"] = toid;
    conn->send(ack.dump());
}
//处理添加好友消息
void ChatService::addfriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id=js["id"].get<int>();
    int friendid=js["friendid"].get<int>();
    FriendModel friendmodel;

    json response;
    response["msgid"] = ADD_FRIEND_ACK;
    if (friendmodel.add(id, friendid))
    {
        response["errno"] = 0;
        LOG_INFO << "user id:" << id << " add friend id:" << friendid;
    }
    else
    {
        response["errno"] = 1;
        response["errmsg"] = "add friend failed";
        LOG_ERROR << "user id:" << id << " add friend id:" << friendid << " failed";
    }
    conn->send(response.dump());
}
//处理创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    Group group;
    group.setName(js["groupname"].get<std::string>());
    group.setDesc(js["groupdesc"].get<std::string>());

    GroupModel groupModel;
    json response;
    response["msgid"] = CREATE_GROUP_ACK;
    if (groupModel.createGroup(group))
    {
        // 创建成功，把创建者加入群组，角色 creator
        groupModel.addGroup(userid, group.getId(), "creator");
        response["errno"] = 0;
        response["groupid"] = group.getId();
        LOG_INFO << "user id:" << userid << " create group id:" << group.getId();
    }
    else
    {
        response["errno"] = 1;
        response["errmsg"] = "create group failed";
    }
    conn->send(response.dump());
}

//处理加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();

    GroupModel groupModel;
    groupModel.addGroup(userid, groupid, "normal");

    json response;
    response["msgid"] = ADD_GROUP_ACK;
    response["errno"] = 0;
    conn->send(response.dump());
    LOG_INFO << "user id:" << userid << " join group id:" << groupid;
}

//处理群聊业务（包含完整的跨服务器转发逻辑）
//
// 功能说明：将群聊消息分发给群组的所有成员（除发送者自己）
//
// 消息分发策略（对每个成员）：
// 1. 成员在本服务器在线 → 直接转发（最优路径）
// 2. 成员不在本服务器在线 → 通过 Redis PUBLISH 发布到成员的频道
// 3. 成员完全离线 → 存储到离线消息表，等上线时推送
//
// 参数：
//   conn - 发送者的 TCP 连接
//   js   - 群聊消息 JSON，包含：
//         - id: 发送者用户ID
//         - groupid: 目标群组ID
//         - msg: 消息内容
//   time - 时间戳
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();           // 发送者ID
    int groupid = js["groupid"].get<int>();       // 群组ID
    std::string msg = js["msg"].get<std::string>(); // 消息内容

    LOG_INFO << "User " << userid << " send group message to group " << groupid;

    // 查询群组所有成员（包括发送者自己，后续会过滤）
    GroupModel groupModel;
    std::vector<GroupUser> members = groupModel.queryGroupUsers(userid, groupid);

    if (members.empty())
    {
        LOG_ERROR << "Group " << groupid << " has no members or user " << userid << " is not a member";
        return;
    }

    // 统计分发结果
    int localOnlineCount = 0;      // 本服务器在线人数
    int redisPublishCount = 0;     // 通过Redis转发的人数
    int offlineCount = 0;          // 离线存储的人数

    // 遍历所有群成员，逐一分发消息
    for (const GroupUser &member : members)
    {
        int memberid = member.getId();  // 当前成员的ID

        // 跳过发送者自己（不需要给自己发消息）
        if (memberid == userid)
        {
            continue;
        }

        // 第一步：检查成员是否在本服务器在线
        TcpConnectionPtr memberConn = nullptr;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _userOnlineMap.find(memberid);
            if (it != _userOnlineMap.end())
            {
                memberConn = it->second;  // 找到成员连接
            }
        }

        if (memberConn)
        {
            // ✅ 情况1：成员在本服务器在线 → 直接转发（最快）
            LOG_INFO << "Group member " << memberid << " is online on this server, send directly";

            json groupMsg;
            groupMsg["msgid"] = GROUP_CHAT_MSG;  // 消息类型：群聊
            groupMsg["id"] = userid;              // 发送者ID
            groupMsg["name"] = js["name"];         // 发送者名称（如果有）
            groupMsg["group"] = groupid;           // 群组ID
            groupMsg["msg"] = msg;                 // 消息内容
            groupMsg["time"] = js["time"];         // 发送时间（如果有）

            memberConn->send(groupMsg.dump());
            localOnlineCount++;
        }
        else
        {
            // ❌ 成员不在本服务器在线，尝试通过 Redis 转发

            // 构造群聊消息体（用于 Redis 转发和离线存储）
            std::string groupMsgContent = js.dump();

            // 第二步：通过 Redis 发布到成员的专属频道
            bool publishSuccess = _redis.publish(memberid, userid, memberid, groupMsgContent);

            if (publishSuccess)
            {
                // Redis 发布成功（可能有其他服务器订阅了该成员的频道）
                LOG_INFO << "Group message published to Redis for member " << memberid;
                redisPublishCount++;

                // 同时存储离线消息作为兜底（防止消息丢失）
                // 注意：这可能导致消息重复，但保证了可靠性
                OfflinMsgModel offlinemsgmodel;
                offlinemsgmodel.insert(memberid, userid, groupMsgContent);
                offlineCount++;  // 记录为离线存储（即使实际被转发了）
            }
            else
            {
                // Redis 发布失败，降级为纯本地离线存储
                LOG_ERROR << "Failed to publish group message via Redis for member " << memberid;

                OfflinMsgModel offlinemsgmodel;
                offlinemsgmodel.insert(memberid, userid, groupMsgContent);
                offlineCount++;
            }
        }
    }

    // 打印分发统计日志
    LOG_INFO << "Group chat distribution summary - "
            << "Total members: " << (members.size() - 1)  // 减去发送者自己
            << ", Local online: " << localOnlineCount
            << ", Redis forwarded: " << redisPublishCount
            << ", Offline stored: " << offlineCount;

    // 返回群聊成功确认给发送者（可选）
    json ack;
    ack["errno"] = 0;
    ack["groupid"] = groupid;
    ack["memberCount"] = (members.size() - 1);  // 实际接收人数
    conn->send(ack.dump());
}

//客户端异常断开
void ChatService::clientClose(const TcpConnectionPtr &conn)
{
    int userId = -1;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto &pair : _userOnlineMap)
        {
            if (pair.second == conn)
            {
                userId = pair.first;
                break;
            }
        }
        if (userId != -1)
            _userOnlineMap.erase(userId);
    }

    if (userId != -1)
    {
        // 取消订阅该用户的Redis频道（用户已离线，不需要再接收跨服务器消息）
        if (!_redis.unsubscribe(userId))
        {
            LOG_ERROR << "Failed to unsubscribe Redis channel for user " << userId;
        }
        else
        {
            LOG_INFO << "Unsubscribed Redis channel for user " << userId;
        }

        // 更新用户状态为离线
        User user;
        user.setId(userId);
        user.setState("offline");
        UserModle userModle;
        userModle.updateState(user);
        LOG_INFO << "user id:" << userId << " disconnected, state set to offline";
    }
}

// 处理从其他服务器通过Redis转发的消息（回调函数）
//
// 此函数在独立线程中被调用（由 Redis::acceptNotifyMsg 触发）
// 当其他服务器通过 Redis PUBLISH 发送消息到本服务器的用户频道时，
// 本服务器的 Redis 订阅连接会收到消息，然后调用此回调处理
//
// 参数：
//   channel - 频道ID（即目标用户ID，应该等于toid）
//   fromid  - 发送者用户ID
//   toid    - 接收者用户ID
//   msg     - 消息内容
//
// 处理逻辑：
// 1. 检查目标用户是否在本服务器在线
//    - 在线 → 直接转发给用户的TcpConnection
//    - 离线 → 存储到离线消息表，等用户上线时推送
void ChatService::handleRedisMessage(int channel, int fromid, int toid, const std::string& msg)
{
    LOG_INFO << "Received message from Redis: from=" << fromid << ", to=" << toid << ", msg=" << msg;

    // 检查目标用户是否在当前服务器在线
    TcpConnectionPtr targetConn = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _userOnlineMap.find(toid);
        if (it != _userOnlineMap.end())
        {
            targetConn = it->second;  // 找到目标用户的连接
        }
    }

    if (targetConn)
    {
        // 目标用户在线，直接转发消息
        LOG_INFO << "User " << toid << " is online, forward message directly";

        json response;
        response["msgid"] = SEND_MSG;
        response["id"] = fromid;      // 发送者ID
        response["msg"] = msg;         // 消息内容
        targetConn->send(response.dump());
    }
    else
    {
        // 目标用户不在线，存储离线消息
        LOG_INFO << "User " << toid << " is offline, save to offline storage";

        OfflinMsgModel _offlinemsgmodel;
        _offlinemsgmodel.insert(toid, fromid, msg);
    }
}
//服务端异常中断
void ChatService::serverClose()
{
    UserModle usermodle;
    usermodle.resetState();
}