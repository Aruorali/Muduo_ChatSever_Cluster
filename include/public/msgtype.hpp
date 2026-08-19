#ifndef MSGTYPE_HPP
#define MSGTYPE_HPP

enum EnMsgType
{
    LOGIN_MSG = 1, //登录消息
    LOGIN_MSG_ACK, //登录响应消息
    REG_MSG,       //注册消息
    REG_MSG_ACK,   //注册响应消息
    SEND_MSG,      //发送消息
};

#endif 