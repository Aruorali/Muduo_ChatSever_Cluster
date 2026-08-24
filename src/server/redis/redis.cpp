#include "redis.hpp"
#include<iostream>
#include<muduo/base/Logging.h>

// 构造函数：初始化Redis连接上下文
// _subscribeCtx: 订阅连接上下文，用于接收频道消息
// _publishCtx: 发布连接上下文，用于向频道发送消息
// 初始化为nullptr，表示尚未建立连接
Redis::Redis()
    : _subscribeCtx(nullptr)
    , _publishCtx(nullptr)
{
}

// 析构函数：释放Redis连接资源
// 检查并释放订阅连接上下文(_subscribeCtx)：
//   - 如果连接存在且存在错误状态，则释放该连接并将指针置空
// 检查并释放发布连接上下文(_publishCtx)：
//   - 同样检查连接是否存在及错误状态，确保资源正确释放
// 注意：这里只释放有错误的连接，正常连接可能由其他机制管理
Redis::~Redis()
{
    if (_subscribeCtx)
    {
        if(_subscribeCtx->err)
        {
            redisFree(_subscribeCtx);
            _subscribeCtx = nullptr;
        }
    }
    if (_publishCtx)
    {
        if(_publishCtx->err)
        {
            redisFree(_publishCtx);
            _publishCtx = nullptr;
        }
    }
}

// 连接Redis服务器，建立发布和订阅两个独立连接
// 订阅连接(_subscribeCtx)：专用于接收频道消息，Redis要求订阅连接不能用于其他操作
// 发布连接(_publishCtx)：专用于向频道发送消息，与订阅连接隔离以避免阻塞
bool Redis::connect()
{
    // 创建订阅连接，连接失败时释放资源并返回false
    _subscribeCtx = redisConnect("127.0.0.1", 6379);
    if(_subscribeCtx->err)
    {
        redisFree(_subscribeCtx);
        _subscribeCtx = nullptr;
        return false;
    }
    // 创建发布连接，连接失败时回滚释放已建立的订阅连接
    _publishCtx = redisConnect("127.0.0.1", 6379);
    if(_publishCtx->err)
    {
        redisFree(_subscribeCtx);
        _subscribeCtx = nullptr;
        return false;
    }
    //创建单独线程监听通道上的事件，有消息给业务层上报 
    std::thread acceptNotifyMsgThread([this](){
        this->acceptNotifyMsg();
    });
    return true;
}

// 订阅指定的Redis频道
// 参数:
//   channel - 要订阅的频道ID（整数类型）
// 返回值:
//   true  - 订阅成功
//   false - 订阅失败
//
// 实现步骤：
// 1. 使用redisAppendCommand将SUBSCRIBE命令添加到订阅连接的输出缓冲区
//    - 该函数不会立即发送命令，而是将命令缓存到缓冲区
//    - 使用_subscribeCtx（订阅专用连接），遵循Redis协议要求
// 2. 循环调用redisBufferWrite刷新缓冲区，直到所有数据发送完毕
//    - down参数用于指示是否还有数据待发送
//    - 循环确保完整的SUBSCRIBE命令被发送到Redis服务器
bool Redis::subscribe(int channel)
{
    // 将SUBSCRIBE命令追加到订阅连接的命令缓冲区
    // 命令格式：SUBSCRIBE <channel_id>
    // REDIS_ERR表示命令添加失败
    if(REDIS_ERR==redisAppendCommand(this->_subscribeCtx,"SUBSCRIBE %d",channel));
    {
        std::cerr<<"subscribe channel error"<<std::endl;
        return false;
    }

    // down标志位：0表示还有数据未发送完成，非0表示发送完毕
    int down=0;

    // 循环写入缓冲区数据到Redis服务器，直到所有数据发送完成
    // redisBufferWrite会尝试从缓冲区写入数据到socket
    // &down参数会在写入完成后被更新，反映缓冲区状态
    while(!down)
    {
        // 尝试写入缓冲区数据
        // REDIS_ERR表示写入过程中发生错误（如网络问题、连接断开等）
        if(REDIS_ERR==redisBufferWrite(this->_subscribeCtx,&down))
        {
            std::cerr<<"subscribe channel error"<<std::endl;
            return false;
        }
    }

    // 订阅命令成功发送到Redis服务器
    return true;
}
// 取消订阅channel
bool Redis::unsubscribe(int channel)
{
    if(REDIS_ERR==redisAppendCommand(this->_subscribeCtx,"UNSUBSCRIBE %d",channel));
    {
        std::cerr<<"unsubscribe channel error"<<std::endl;
        return false;
    }

    // down标志位：0表示还有数据未发送完成，非0表示发送完毕
    int down=0;

    // 循环写入缓冲区数据到Redis服务器，直到所有数据发送完成
    // redisBufferWrite会尝试从缓冲区写入数据到socket
    // &down参数会在写入完成后被更新，反映缓冲区状态
    while(!down)
    {
        // 尝试写入缓冲区数据
        // REDIS_ERR表示写入过程中发生错误（如网络问题、连接断开等）
        if(REDIS_ERR==redisBufferWrite(this->_subscribeCtx,&down))
        {
            std::cerr<<"unsubscribe channel error"<<std::endl;
            return false;
        }
    }
}
// 发布消息
// 发布消息到指定频道（跨服务器消息转发）
//
// 功能说明：
//   当需要给其他服务器的用户发送消息时调用此函数
//   消息会通过 Redis PUBLISH 命令发送到目标用户ID对应的频道
//   目标用户所在的服务器如果订阅了该频道，就能收到这条消息
//
// 参数：
//   channel - 目标用户ID（作为Redis频道名）
//            设计思路：每个用户有一个专属频道，格式为 user_{userid}
//            当用户在某台服务器登录时，该服务器订阅用户的频道
//   fromid  - 发送者用户ID
//   toid    - 接收者用户ID（应该等于channel参数）
//   msg     - 消息内容（纯文本或JSON字符串）
//
// Redis命令格式：PUBLISH <channel> <message>
//   message 格式为："fromid:toid:msg" （用冒号分隔，便于解析）
//
// 返回值：
//   true  - 消息发布成功
//   false - 发布失败（Redis连接错误等）
//
// 使用示例：
//   // 用户1在Server-A，要给用户2发消息，但用户2不在Server-A
//   // 调用此函数将消息发布到用户2的频道
//   redis.publish(2, 1, 2, "你好");
//   // Server-B（用户2所在服务器）订阅了频道2，会收到这条消息
bool Redis::publish(int channel, int fromid, int toid, const std::string &msg)
{
    // 构造消息体：fromid:toid:msg （用冒号作为分隔符）
    // 这样接收端可以方便地解析出三个字段
    std::string message = std::to_string(fromid) + ":" + std::to_string(toid) + ":" + msg;

    redisReply *reply = nullptr;

    // 执行 Redis PUBLISH 命令
    // 命令格式：PUBLISH <channel> <message>
    //   channel: 目标用户ID（整数）
    //   message: "fromid:toid:msg" 格式的字符串
    reply = (redisReply *)redisCommand(
        this->_publishCtx,
        "PUBLISH %d %s",
        channel,
        message.c_str()
    );

    if(reply==nullptr)
    {
        std::cerr<<"publish channel error"<<std::endl;
        return false;
    }

    // reply->integer 表示订阅了该频道的客户端数量
    // 如果为0，说明没有服务器在线监听该频道（目标用户可能离线）
    if (reply->integer == 0)
    {
        LOG_INFO << "publish to channel " << channel << ", but no subscriber online";
    }

    freeReplyObject(reply);
    return true;
}
// 在独立线程中接收Redis订阅消息
// 该函数应运行在独立线程中，持续监听订阅连接上收到的消息
//
// 在独立线程中接收Redis订阅消息
// 该函数应运行在独立线程中，持续监听订阅连接上收到的消息
//
// 实现原理：
// 1. redisGetReply 从订阅连接(_subscribeCtx)阻塞获取Redis回复
// 2. 订阅消息的回复格式为数组类型，包含三个元素：
//    - element[0]: "message"（消息类型标识）
//    - element[1]: 频道名称（即目标用户ID）
//    - element[2]: 消息内容（格式为 "fromid:toid:msg"）
// 3. 解析消息内容，提取 fromid、toid、msg 三个字段
// 4. 调用回调函数 _notifyMsgHandler 通知上层业务处理
// 5. 当连接断开或出错时，redisGetReply 返回非 OK，循环退出
//
// 注意：此函数会阻塞当前线程，必须在独立线程中调用
void Redis::acceptNotifyMsg()
{
    // Redis回复对象指针，用于接收每次的回复数据
    redisReply *reply = nullptr;

    // 循环从订阅连接获取回复，直到连接断开或出错
    // redisGetReply 会阻塞等待直到有数据可读
    // REDIS_OK 表示成功获取到一条回复
    while(REDIS_OK==redisGetReply(this->_subscribeCtx,(void **)&reply))
    {
        // 校验回复数据的完整性：
        // 1. reply != nullptr          - 回复对象存在
        // 2. element[1] != nullptr     - 频道名称存在
        // 3. element[2] != nullptr     - 消息内容存在
        // 4. element[2]->str != nullptr - 消息字符串内容不为空
        if(reply!=nullptr &&
           reply->element[1]!=nullptr &&
           reply->element[2]!=nullptr &&
           reply->element[2]->str!=nullptr)
        {
            // 提取频道ID（即目标用户ID）
            int channel = atoi(reply->element[1]->str);

            // 解析消息内容："fromid:toid:msg"
            std::string message = reply->element[2]->str;
            int fromid = 0;
            int toid = 0;
            std::string msg;

            // 按冒号分隔解析三个字段
            size_t pos1 = message.find(':');
            if (pos1 != std::string::npos)
            {
                fromid = atoi(message.substr(0, pos1).c_str());
                size_t pos2 = message.find(':', pos1 + 1);
                if (pos2 != std::string::npos)
                {
                    toid = atoi(message.substr(pos1 + 1, pos2 - pos1 - 1).c_str());
                    msg = message.substr(pos2 + 1);
                }
            }

            // 调用消息通知回调函数，传递四个参数：
            // - channel: 频道ID（目标用户ID）
            // - fromid:  发送者用户ID
            // - toid:    接收者用户ID
            // - msg:     消息内容
            _notifyMsgHandler(channel, fromid, toid, msg);
        }
    }

    // 循环退出意味着连接断开或出错，打印退出日志
    std::cerr<<">>>>>>>>>>> acceptNotifyMsg quit <<<<<<<<<<<<"<<std::endl;
}

// 设置回调函数（收到跨服务器消息时调用）
// 参数说明：
//   handler - 回调函数，签名为 void(int channel, int fromid, int toid, string msg)
//   - channel: 收到消息的频道ID（即目标用户ID）
//   - fromid:  发送者用户ID
//   - toid:    接收者用户ID
//   - msg:     消息内容
void Redis::setNotifyMsgHandler(std::function<void(int, int, int, std::string)> handler)
{
    _notifyMsgHandler = handler;
}