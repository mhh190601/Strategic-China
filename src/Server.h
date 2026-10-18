#pragma once

#include "World.h"
#include "UDPSocket.h"
#include "NetworkMessage.h"
#include <string>
#include <vector>

// 主机权威服务器：运行完整游戏世界，接收客户端输入并应用到对应队伍，
// 以固定频率向所有已连接客户端广播游戏状态。
class Server {
public:
    // 主机控制的玩家 ID（team 0）。客户端玩家为 team 1。
    static constexpr int HOST_PLAYER_ID = 0;
    static constexpr int CLIENT_PLAYER_ID = 1;
    static constexpr unsigned short PORT = 12345;
    static constexpr float BROADCAST_INTERVAL = 0.05f; // 20Hz 状态广播

    Server();
    ~Server();

    // 启动：创建并绑定 socket，布置初始世界
    bool Start();

    // 应用主机本地的玩家输入（与客户端输入走同一处理逻辑）
    void ApplyLocalInput(const InputPayload& input);

    // 推进一帧：接收输入、更新世界、广播状态
    // 返回当前胜负结果（0 继续 / 1 主机胜 / 2 主机败）
    int Update(float dt);

    // 渲染（直接复用 World 的渲染）
    void Render(SDL_Renderer* renderer, int selectedIndex);

    World& GetWorld() { return world_; }

    // 是否已有客户端加入（未加入前主机冻结世界、显示等待界面）
    bool IsClientJoined() const { return clientJoined_; }

    // 已加入客户端数量
    int GetClientCount() const { return (int)clients_.size(); }

private:
    // 客户端网络端点（IP + 端口）
    // 注意：必须记录客户端实际的源端口，不能固定发往 PORT——客户端绑定的是随机端口，
    // 固定端口会导致客户端永远收不到主机状态（同机时主机甚至会收到自己发出去的包）
    struct ClientEndpoint {
        std::string ip;
        unsigned short port = 0;
    };

    UDPSocket socket_;
    World world_;
    float broadcastTimer_ = 0.0f; // 状态广播计时器
    std::vector<ClientEndpoint> clients_; // 已加入的客户端端点列表
    bool clientJoined_ = false;   // 是否有客户端加入（false 时世界不更新）
    int lastSentResult_ = 0;      // 上次广播的胜负结果（变化时打印一次，便于排查）
    Uint64 lastLogTicks_ = 0;     // 日志节流时间戳（避免 20Hz 刷屏）

    // 解析并应用一条输入（同时用于本地与远程）
    void ApplyInput(const InputPayload& input);

    // 向所有客户端广播当前状态
    void BroadcastState();

    // 处理入站 UDP 数据（Join / Input）
    void ProcessIncoming();

    // 日志节流：同一条日志在该间隔内只打印一次；设为 0 则每次都打印
    static constexpr Uint64 LOG_THROTTLE_MS = 500;
};
