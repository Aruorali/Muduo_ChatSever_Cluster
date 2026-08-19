#ifndef MSGTYPE_HPP
#define MSGTYPE_HPP

enum EnMsgType
{
    LOGIN_MSG = 1, //登录消息
    LOGIN_MSG_ACK, //登录响应消息
    REG_MSG,       //注册消息
    REG_MSG_ACK,   //注册响应消息
    SEND_MSG,      //发送消息
    ADD_FRIEND_MSG,//添加好友消息
    ADD_FRIEND_ACK,//添加好友响应消息
    FRIEND_LIST_MSG,//登录成功推送好友列表
};

#endif 