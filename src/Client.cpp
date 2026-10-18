#include "Client.h"

#include <algorithm>
#include <cstdio>

Client::Client() = default;
Client::~Client() = default;

bool Client::Connect(const std::string& hostIp) {
    hostIp_ = hostIp;

    if (!socket_.Open()) {
        printf("[Client] 错误：无法创建 UDP 套接字（错误码: %d）\n", UDPSocket::GetLastErrorCode());
        fflush(stdout);
        return false;
    }
    if (!socket_.Bind(0)) { // 客户端绑定任意可用端口
        printf("[Client] 错误：无法绑定本地端口（错误码: %d）\n", UDPSocket::GetLastErrorCode());
        fflush(stdout);
        return false;
    }

    // 发送 Join 请求，告知主机本玩家 ID
    auto buf = NetworkMessage::SerializeJoin(CLIENT_PLAYER_ID);
    int sent = socket_.SendTo(hostIp_, PORT, buf.data(), (int)buf.size());
    if (sent < 0) {
        printf("[Client] 错误：加入请求发送失败（错误码: %d），请检查主机 IP 是否正确: %s:%u\n",
               UDPSocket::GetLastErrorCode(), hostIp_.c_str(), (unsigned)PORT);
        fflush(stdout);
        return false;
    }

    lastJoinSentTicks_ = SDL_GetTicks();
    printf("[Client] 已向主机 %s:%u 发送加入请求，等待主机状态...\n",
           hostIp_.c_str(), (unsigned)PORT);
    fflush(stdout);
    return true;
}

void Client::SendInput(const InputPayload& input) {
    auto buf = NetworkMessage::SerializeInput(input);
    socket_.SendTo(hostIp_, PORT, buf.data(), (int)buf.size());
}

bool Client::ReceiveState() {
    // 尚未收到主机状态：定期重发 Join（UDP 不可靠，且主机可能比客户端晚启动）
    if (!joined_) {
        Uint64 now = SDL_GetTicks();
        if (now - lastJoinSentTicks_ >= JOIN_RETRY_MS) {
            lastJoinSentTicks_ = now;
            auto jb = NetworkMessage::SerializeJoin(CLIENT_PLAYER_ID);
            socket_.SendTo(hostIp_, PORT, jb.data(), (int)jb.size());
        }
    }

    unsigned char buf[4096];
    std::string ip;
    unsigned short port = 0;
    int len = socket_.RecvFrom(buf, sizeof(buf), ip, port);
    bool got = false;
    while (len > 0) {
        if (buf[0] == (unsigned char)MsgType::State) {
            std::vector<UnitSnapshot> snaps;
            std::vector<ObstacleSnapshot> obs;
            int win = 0;
            if (NetworkMessage::DeserializeState(buf, len, snaps, &obs, &win)) {
                winResult_ = win; // 以主机的权威判定为准
                RebuildLocalUnits(snaps);
                RebuildLocalObstacles(obs);
                got = true;

                // 首次收到状态：说明联机真正建立
                if (!joined_) {
                    joined_ = true;
                    printf("[Client] 已连接主机 %s:%u，开始同步\n", ip.c_str(), (unsigned)port);
                    fflush(stdout);
                }

                // 主机结果变化时立即打印一次
                if (win != lastWinResult_) {
                    lastWinResult_ = win;
                    printf("[Client] 主机下发结果: %d（%s）\n", win,
                           win == 1 ? "主机胜/本机败" : (win == 2 ? "主机败/本机胜" : "继续"));
                    fflush(stdout);
                }

                // 调试输出：确认状态同步是否正常（节流，避免刷屏）
                Uint64 now = SDL_GetTicks();
                if (LOG_THROTTLE_MS == 0 || now - lastLogTicks_ >= LOG_THROTTLE_MS) {
                    lastLogTicks_ = now;
                    int blue = 0, red = 0;
                    for (const auto& s : snaps) { if (s.team == 0) ++blue; else ++red; }
                    printf("[Client] 收到状态，单位数: %d（蓝 %d / 红 %d，结果 %d）\n",
                           (int)snaps.size(), blue, red, win);
                    fflush(stdout);
                }
            }
        }
        len = socket_.RecvFrom(buf, sizeof(buf), ip, port);
    }

    // 本轮确实收到过状态：刷新插值计时（一帧只更新一次，避免多包干扰间隔估计）
    if (got) UpdateInterpTiming();

    return got;
}

void Client::RebuildLocalUnits(const std::vector<UnitSnapshot>& snaps) {
    // ---- 先更新插值状态 ----
    // 关键：把"当前正在渲染的位置"作为新的起点 prev，这样新快照到来时不会跳变；
    // 新出现的单位（无插值记录）直接出现在目标点，避免从原点飞过来。
    const float alpha = CurrentAlpha();
    std::unordered_map<int, UnitInterp> newInterp;
    newInterp.reserve(snaps.size());
    for (const auto& s : snaps) {
        UnitInterp ip;
        auto it = interp_.find(s.id);
        if (it != interp_.end()) {
            ip.prevX = it->second.prevX + (it->second.targetX - it->second.prevX) * alpha;
            ip.prevY = it->second.prevY + (it->second.targetY - it->second.prevY) * alpha;
        } else {
            ip.prevX = s.x; // 新单位：起点即目标点
            ip.prevY = s.y;
        }
        ip.targetX = s.x;
        ip.targetY = s.y;
        newInterp[s.id] = ip;
    }
    interp_.swap(newInterp); // 已消失（死亡/移除）的单位其插值记录随之丢弃

    // ---- 再重建渲染用的单位镜像 ----
    localUnits_.clear();
    localUnits_.reserve(snaps.size());
    for (const auto& s : snaps) {
        SDL_Color c = (s.team == 0) ? SDL_Color{ 60, 120, 255, 255 }
                                    : SDL_Color{ 255, 60, 60, 255 };
        Unit u(s.x, s.y, s.radius, c, s.team, s.health, 200.0f);
        u.id = s.id;
        u.health = s.health;
        u.maxHealth = 100.0f;
        u.isAlive = s.isAlive;
        localUnits_.push_back(u);
    }
}

// 当前插值进度：从上次收到快照到现在经过的时间 / 估计的快照间隔，钳制到 [0,1]
float Client::CurrentAlpha() const {
    if (lastSnapshotTicks_ == 0) return 1.0f; // 还没收到过状态：直接停在目标点
    float dtMs = (float)(SDL_GetTicks() - lastSnapshotTicks_);
    float a = dtMs / snapshotIntervalMs_;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f; // 快照迟到时停在目标点，不追赶也不回退
    return a;
}

// 取插值后的渲染坐标
bool Client::GetRenderPos(int id, float& outX, float& outY) const {
    auto it = interp_.find(id);
    if (it == interp_.end()) return false;
    const float a = CurrentAlpha();
    const UnitInterp& ip = it->second;
    outX = ip.prevX + (ip.targetX - ip.prevX) * a;
    outY = ip.prevY + (ip.targetY - ip.prevY) * a;
    return true;
}

// 收到新快照后更新计时，并用指数平滑自适应估计真实广播间隔
void Client::UpdateInterpTiming() {
    Uint64 now = SDL_GetTicks();
    if (lastSnapshotTicks_ != 0) {
        float delta = (float)(now - lastSnapshotTicks_);
        // 只接受合理区间内的样本，忽略卡顿或重复包带来的极端值
        if (delta > 5.0f && delta < 500.0f) {
            snapshotIntervalMs_ = snapshotIntervalMs_ * 0.8f + delta * 0.2f;
            if (snapshotIntervalMs_ < 10.0f) snapshotIntervalMs_ = 10.0f;
        }
    }
    lastSnapshotTicks_ = now;
}

void Client::RebuildLocalObstacles(const std::vector<ObstacleSnapshot>& obs) {
    localObstacles_.clear();
    localObstacles_.reserve(obs.size());
    for (const auto& o : obs) {
        // 与主机保持一致的灰色矩形
        localObstacles_.push_back(Obstacle(o.x, o.y, o.w, o.h));
    }
}

void Client::Render(SDL_Renderer* renderer, int selectedIndex) {
    // 先绘制障碍物（灰色矩形），与主机场景一致
    for (const auto& ob : localObstacles_) {
        SDL_FRect rect = { ob.x, ob.y, ob.w, ob.h };
        SDL_SetRenderDrawColor(renderer, ob.color.r, ob.color.g, ob.color.b, ob.color.a);
        SDL_RenderFillRect(renderer, &rect);
    }

    // 客户端渲染：圆 + 血条 + 选中高亮
    // 位置使用插值结果：在 prev 与 target 之间按时间比例平滑过渡
    for (int i = 0; i < (int)localUnits_.size(); ++i) {
        auto& u = localUnits_[i];
        if (!u.isAlive) continue;

        // 取插值后的渲染坐标（没有插值记录时退回快照坐标）
        float rx = u.x, ry = u.y;
        if (GetRenderPos(u.id, rx, ry)) {
            // 同步回镜像，保证点选判定也用玩家看到的平滑位置
            u.x = rx;
            u.y = ry;
        }

        DrawFillCircle(renderer, rx, ry, u.radius, u.color);

        // 血条（红底 + 绿条）
        float barW = u.radius * 2.0f;
        float barH = 5.0f;
        SDL_FRect bg = { rx - u.radius, ry - u.radius - 10.0f, barW, barH };
        SDL_SetRenderDrawColor(renderer, 200, 40, 40, 255);
        SDL_RenderFillRect(renderer, &bg);
        float ratio = (u.maxHealth > 0) ? (u.health / u.maxHealth) : 0.0f;
        ratio = (ratio < 0) ? 0 : (ratio > 1 ? 1 : ratio);
        SDL_FRect fg = { rx - u.radius, ry - u.radius - 10.0f, barW * ratio, barH };
        SDL_SetRenderDrawColor(renderer, 60, 220, 60, 255);
        SDL_RenderFillRect(renderer, &fg);

        // 选中高亮（白色圈）
        if (i == selectedIndex) {
            SDL_FRect ring = { rx - u.radius - 3.0f, ry - u.radius - 3.0f,
                               u.radius * 2.0f + 6.0f, u.radius * 2.0f + 6.0f };
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderRect(renderer, &ring);
        }
    }
}
