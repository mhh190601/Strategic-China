#pragma once

#include <string>
#include <vector>

// UDP 套接字封装：基于 Windows Winsock2，使用非阻塞模式
// 负责 socket 的创建、绑定、发送、接收与关闭，供 Server/Client 复用
class UDPSocket {
public:
    UDPSocket();
    ~UDPSocket();

    // 初始化 Winsock 并创建 UDP socket（非阻塞）
    // 返回 true 表示成功
    bool Open();

    // 绑定到指定端口（主机调用，监听入站数据）
    bool Bind(unsigned short port);

    // 关闭 socket 并清理 Winsock
    void Close();

    // 发送数据到指定地址（IP 字符串 + 端口）
    // 返回实际发送的字节数，失败返回 -1
    int SendTo(const std::string& ip, unsigned short port,
               const unsigned char* data, int len);

    // 接收数据，填充 senderIp / senderPort
    // 返回收到的字节数，无数据（非阻塞）返回 0，出错返回 -1
    int RecvFrom(unsigned char* buffer, int maxLen,
                 std::string& senderIp, unsigned short& senderPort);

    // 获取最近一次底层错误码（Windows 为 WSAGetLastError），用于打印连接失败原因
    static int GetLastErrorCode();

private:
    class Impl;        // 用 PImpl 隐藏 Winsock 细节，避免头文件暴露 Windows 类型
    Impl* impl_ = nullptr;
};
