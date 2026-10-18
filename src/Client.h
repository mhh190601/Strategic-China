#pragma once

#include "UDPSocket.h"
#include "NetworkMessage.h"
#include "World.h" // 复用 Unit / DrawFillCircle 渲染
#include <string>
#include <vector>
#include <unordered_map>

// 单个单位的插值状态：prev = 上一次的当前渲染位置，target = 最新快照位置
struct UnitInterp {
    float prevX = 0.0f;
    float prevY = 0.0f;
    float targetX = 0.0f;
    float targetY = 0.0f;
};

// 客户端：发送玩家输入，接收服务器状态并渲染本地世界（无权威逻辑）
// 位置采用"prev -> target"按时间线性插值，避免 20Hz 状态造成的 50ms 跳变
class Client {
public:
    static constexpr int CLIENT_PLAYER_ID = 1;
    static constexpr unsigned short PORT = 12345;

    Client();
    ~Client();

    // 连接到指定主机 IP（默认 127.0.0.1）
    bool Connect(const std::string& hostIp = "127.0.0.1");

    // 发送一条玩家输入
    void SendInput(const InputPayload& input);

    // 接收并处理服务器状态（更新本地渲染单位列表）
    // 返回 true 表示本轮收到了新状态
    bool ReceiveState();

    // 用最新状态渲染本地世界
    void Render(SDL_Renderer* renderer, int selectedIndex);

    // 从快照重建/更新本地单位列表（简单直接替换）
    const std::vector<Unit>& GetLocalUnits() const { return localUnits_; }

    // 是否已收到主机状态（收到前显示"等待主机"界面）
    bool HasJoined() const { return joined_; }

    // 主机权威判定的胜负结果（0 继续 / 1 主机胜 / 2 主机败）
    int GetWinResult() const { return winResult_; }

private:
    UDPSocket socket_;
    std::string hostIp_;
    std::vector<Unit> localUnits_; // 仅用于渲染的本地镜像（单位）
    std::vector<Obstacle> localObstacles_; // 仅用于渲染的本地镜像（障碍物/围墙）
    bool joined_ = false;          // 是否已收到主机状态（即联机真正建立）
    int winResult_ = 0;            // 主机下发的胜负结果（0 继续 / 1 主机胜 / 2 主机败）
    int lastWinResult_ = 0;        // 上一次的结果（变化时打印一次，便于排查）
    Uint64 lastJoinSentTicks_ = 0; // 上次发送 Join 的时间戳（用于自动重试）
    Uint64 lastLogTicks_ = 0;      // 日志节流时间戳

    // ---- 插值平滑相关 ----
    std::unordered_map<int, UnitInterp> interp_; // 按单位 id 保存的插值状态
    Uint64 lastSnapshotTicks_ = 0;   // 上一次收到状态快照的时刻
    float snapshotIntervalMs_ = 50.0f; // 状态间隔估计（自适应平滑，初值 50ms）

    // 当前插值进度 0~1（0=停在 prev，1=到达 target）
    float CurrentAlpha() const;

    // 取某单位插值后的渲染坐标；不存在该单位返回 false
    bool GetRenderPos(int id, float& outX, float& outY) const;

    // 收到新快照后更新插值计时（自适应估计广播间隔）
    void UpdateInterpTiming();

    // 用快照填充本地单位列表（颜色按队伍给，保持与主机一致）
    void RebuildLocalUnits(const std::vector<UnitSnapshot>& snaps);

    // 用快照填充本地障碍物列表（围墙等，保证客户端地图与主机一致）
    void RebuildLocalObstacles(const std::vector<ObstacleSnapshot>& obs);

    // 日志节流：同一条日志在该间隔内只打印一次；设为 0 则每次都打印
    static constexpr Uint64 LOG_THROTTLE_MS = 500;
    // Join 重试间隔：UDP 不可靠且主机可能晚启动，未收到状态前定期重发
    static constexpr Uint64 JOIN_RETRY_MS = 500;
};
