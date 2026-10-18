#include "UDPSocket.h"

#ifdef _WIN32
// Windows 平台：使用 Winsock2 实现 UDP 通信
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") // 链接 Winsock 库（同时也会在 vcxproj 显式添加）

class UDPSocket::Impl {
public:
    SOCKET sock = INVALID_SOCKET;
    bool initialized = false;
};

UDPSocket::UDPSocket() : impl_(new Impl()) {}

UDPSocket::~UDPSocket() {
    Close();
}

bool UDPSocket::Open() {
    // 初始化 Winsock（版本 2.2）
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err != 0) return false;
    impl_->initialized = true;

    impl_->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->sock == INVALID_SOCKET) {
        WSACleanup();
        impl_->initialized = false;
        return false;
    }

    // 设为非阻塞模式
    u_long nonBlocking = 1;
    if (ioctlsocket(impl_->sock, FIONBIO, &nonBlocking) != 0) {
        closesocket(impl_->sock);
        impl_->sock = INVALID_SOCKET;
        WSACleanup();
        impl_->initialized = false;
        return false;
    }
    return true;
}

bool UDPSocket::Bind(unsigned short port) {
    if (impl_->sock == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网卡
    addr.sin_port = htons(port);
    if (bind(impl_->sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        return false;
    }
    return true;
}

void UDPSocket::Close() {
    if (impl_ && impl_->sock != INVALID_SOCKET) {
        closesocket(impl_->sock);
        impl_->sock = INVALID_SOCKET;
    }
    if (impl_ && impl_->initialized) {
        WSACleanup();
        impl_->initialized = false;
    }
    delete impl_;
    impl_ = nullptr;
}

int UDPSocket::SendTo(const std::string& ip, unsigned short port,
                      const unsigned char* data, int len) {
    if (impl_->sock == INVALID_SOCKET) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // 字符串 IP -> 网络地址
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        return -1;
    }
    int sent = sendto(impl_->sock, (const char*)data, len, 0,
                      (sockaddr*)&addr, sizeof(addr));
    return sent;
}

int UDPSocket::RecvFrom(unsigned char* buffer, int maxLen,
                       std::string& senderIp, unsigned short& senderPort) {
    if (impl_->sock == INVALID_SOCKET) return -1;
    sockaddr_in from{};
    int fromLen = sizeof(from);
    int received = recvfrom(impl_->sock, (char*)buffer, maxLen, 0,
                            (sockaddr*)&from, &fromLen);
    if (received == SOCKET_ERROR) {
        int wsaErr = WSAGetLastError();
        // 非阻塞下无数据或稍纵即逝的阻塞都视为"暂无数据"
        if (wsaErr == WSAEWOULDBLOCK || wsaErr == WSAECONNRESET) return 0;
        return -1;
    }
    // 将发送方地址转换回字符串与端口
    char ipStr[INET_ADDRSTRLEN] = { 0 };
    inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));
    senderIp = ipStr;
    senderPort = ntohs(from.sin_port);
    return received;
}

int UDPSocket::GetLastErrorCode() {
    return WSAGetLastError();
}

#else
// 非 Windows 平台（如 Linux/macOS）的占位实现，保证头文件可移植编译
class UDPSocket::Impl {
public:
    int sock = -1;
};
UDPSocket::UDPSocket() : impl_(new Impl()) {}
UDPSocket::~UDPSocket() { Close(); }
bool UDPSocket::Open() { return false; }
bool UDPSocket::Bind(unsigned short) { return false; }
void UDPSocket::Close() { delete impl_; impl_ = nullptr; }
int UDPSocket::SendTo(const std::string&, unsigned short,
                      const unsigned char*, int) { return -1; }
int UDPSocket::RecvFrom(unsigned char*, int, std::string&, unsigned short&) { return -1; }
int UDPSocket::GetLastErrorCode() { return 0; }
#endif
