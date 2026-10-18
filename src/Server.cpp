#include "Server.h"

#include <algorithm>
#include <cstdio>

Server::Server() = default;
Server::~Server() = default;

bool Server::Start() {
    if (!socket_.Open()) {
        printf("[Server] 错误：无法创建 UDP 套接字（错误码: %d）\n", UDPSocket::GetLastErrorCode());
        fflush(stdout);
        return false;
    }
    if (!socket_.Bind(PORT)) {
        printf("[Server] 错误：无法绑定端口 %u（错误码: %d），端口可能被其它实例占用\n",
               (unsigned)PORT, UDPSocket::GetLastErrorCode());
        fflush(stdout);
        return false;
    }

    printf("[Server] 主机已启动，监听端口 %u，等待客户端加入...\n", (unsigned)PORT);
    fflush(stdout);

    // 布置初始世界：主机 team 0，客户端 team 1，外加中立 AI 行为
    const SDL_Color blue = { 60, 120, 255, 255 };
    const SDL_Color red = { 255, 60, 60, 255 };
    auto mk = [&](float x, float y, float r, SDL_Color c, int team, float dmg, float range, float cd) {
        Unit u(x, y, r, c, team, 100.0f, 200.0f);
        u.attackDamage = dmg; u.attackRange = range; u.attackCooldown = cd;
        world_.AddUnit(u);
    };
    // 主机方（team 0）3 个
    mk(220.0f, 180.0f, 22.0f, blue, 0, 20.0f, 80.0f, 1.0f);
    mk(340.0f, 260.0f, 22.0f, blue, 0, 20.0f, 80.0f, 1.0f);
    mk(240.0f, 360.0f, 22.0f, blue, 0, 20.0f, 80.0f, 1.0f);
    // 客户端方（team 1）3 个（联机时由客户端控制，单机测试时由 AI 控制）
    mk(600.0f, 200.0f, 22.0f, red, 1, 20.0f, 80.0f, 1.0f);
    mk(620.0f, 420.0f, 22.0f, red, 1, 20.0f, 80.0f, 1.0f);
    mk(560.0f, 320.0f, 22.0f, red, 1, 20.0f, 80.0f, 1.0f);

    // 障碍物
    world_.AddObstacle(Obstacle(300.0f, 280.0f, 200.0f, 50.0f));
    world_.AddObstacle(Obstacle(80.0f, 80.0f, 120.0f, 30.0f));
    world_.AddObstacle(Obstacle(560.0f, 460.0f, 30.0f, 120.0f));
    world_.AddObstacle(Obstacle(400.0f, 80.0f, 30.0f, 90.0f));

    return true;
}

void Server::ApplyLocalInput(const InputPayload& input) {
    ApplyInput(input);
}

int Server::Update(float dt) {
    // 1. 处理入站输入（客户端发来的 Join / Input）
    ProcessIncoming();

    // 2. 客户端尚未加入：冻结世界（AI 不行动、不移动、不战斗），也不广播状态
    if (!clientJoined_) {
        // 每 2 秒打一次心跳，确认主机确实在轮询收包（便于排查"客户端连不上"）
        Uint64 now = SDL_GetTicks();
        if (now - lastLogTicks_ >= 2000) {
            lastLogTicks_ = now;
            printf("[Server] 等待客户端加入中...（监听端口 %u）\n", (unsigned)PORT);
            fflush(stdout);
        }
        return 0;
    }

    // 3. 胜负已分后冻结世界（AI、移动、战斗都不再推进），
    //    但必须继续广播，否则客户端永远收不到"最后一个单位已死"的最终状态
    int result = world_.CheckWinCondition();
    if (result == 0) {
        // 推进游戏世界（AI、移动、碰撞、战斗、移除死亡）
        world_.Update(dt);
        result = world_.CheckWinCondition();
    }

    // 4. 固定频率广播状态（结束后同样广播，客户端靠它同步清场与最终结果）
    broadcastTimer_ += dt;
    if (broadcastTimer_ >= BROADCAST_INTERVAL) {
        broadcastTimer_ = 0.0f;
        BroadcastState();
    }

    // 5. 返回胜负（约定：0 继续 / 1 主机胜 / 2 主机败）
    return result;
}

void Server::Render(SDL_Renderer* renderer, int selectedIndex) {
    world_.Render(renderer, selectedIndex);
}

void Server::ApplyInput(const InputPayload& input) {
    // 仅允许控制"发送方对应队伍"的单位：主机(playerId=0)->team0，客户端(playerId=1)->team1
    int myTeam = (input.playerId == HOST_PLAYER_ID) ? 0 : 1;

    Unit* u = world_.FindUnitById(input.byUnitId);
    if (!u || !u->isAlive || u->team != myTeam) return; // 越权或无效单位忽略

    if (input.op == InputOp::Select) {
        // 选择仅影响客户端侧的选中高亮，这里标记为"被客户端选中"（用 attackTimer 占位无关）
        // 主机侧选中由本地逻辑处理，此处无需额外操作
    } else if (input.op == InputOp::Move) {
        // 右键移动：设置移动目标（同时清攻击目标）
        u->ClearAttackTarget();
        u->SetTarget(input.x, input.y);
    } else if (input.op == InputOp::Attack) {
        // 左键设攻击目标：按目标单位 id 找索引
        Unit* target = world_.FindUnitById(input.targetUnitId);
        if (target && target->isAlive) {
            // 用 id 反查当前数组索引（CombatSystem 按索引工作，但索引会随死亡重排，
            // 因此这里每次都重新解析，并依赖 World::Update 的重映射保持正确）
            const auto& units = world_.GetUnits();
            for (int i = 0; i < (int)units.size(); ++i) {
                if (units[i].id == input.targetUnitId) {
                    u->SetAttackTarget(i);
                    break;
                }
            }
        }
    }
}

void Server::BroadcastState() {
    std::vector<UnitSnapshot> snaps;
    int aliveBlue = 0, aliveRed = 0, deadSkipped = 0;
    for (const auto& u : world_.GetUnits()) {
        // 防御：任何情况下都不把死亡单位发给客户端（理论上 units_ 已清理干净）
        if (!u.isAlive) { ++deadSkipped; continue; }
        if (u.team == 0) ++aliveBlue; else ++aliveRed;
        UnitSnapshot s;
        s.id = u.id;
        s.x = u.x; s.y = u.y; s.radius = u.radius;
        s.team = u.team; s.health = u.health; s.isAlive = u.isAlive;
        snaps.push_back(s);
    }
    // 附带障碍物，保证客户端能画出与主机一致的地图
    std::vector<ObstacleSnapshot> obs;
    for (const auto& ob : world_.GetObstacles()) {
        ObstacleSnapshot o;
        o.x = ob.x; o.y = ob.y; o.w = ob.w; o.h = ob.h;
        obs.push_back(o);
    }
    // 附带主机权威判定的胜负结果，让客户端直接同步结束（不靠客户端本地猜测）
    int winResult = world_.CheckWinCondition();
    auto buf = NetworkMessage::SerializeState(snaps, obs, winResult);
    // 向每个已知客户端广播（使用客户端真实的源端口）
    for (const auto& c : clients_) {
        socket_.SendTo(c.ip, c.port, buf.data(), (int)buf.size());
    }

    // 胜负结果发生变化时立即打印一次，便于确认主机确实判出结果
    if (winResult != lastSentResult_) {
        lastSentResult_ = winResult;
        printf("[Server] 胜负已分，结果: %d（%s）蓝 %d / 红 %d\n",
               winResult,
               winResult == 1 ? "主机胜" : (winResult == 2 ? "主机败" : "继续"),
               aliveBlue, aliveRed);
        fflush(stdout);
    }

    // 调试输出：确认广播是否正常（节流，避免 20Hz 刷屏）
    if (clients_.empty()) return; // 无客户端时不必打印
    Uint64 now = SDL_GetTicks();
    if (LOG_THROTTLE_MS == 0 || now - lastLogTicks_ >= LOG_THROTTLE_MS) {
        lastLogTicks_ = now;
        printf("[Server] 广播状态，单位数: %d（蓝 %d / 红 %d，结果 %d%s）\n",
               (int)snaps.size(), aliveBlue, aliveRed, winResult,
               deadSkipped > 0 ? "，已过滤死亡单位" : "");
        fflush(stdout);
    }
}

void Server::ProcessIncoming() {
    unsigned char buf[2048];
    std::string ip;
    unsigned short port = 0;
    int len = socket_.RecvFrom(buf, sizeof(buf), ip, port);
    while (len > 0) {
        if (len > 0 && buf[0] == (unsigned char)MsgType::Join) {
            int pid = 0;
            if (NetworkMessage::DeserializeJoin(buf, len, pid)) {
                // 记录新客户端（按 IP + 端口 去重）
                bool isNew = true;
                for (const auto& c : clients_) {
                    if (c.ip == ip && c.port == port) { isNew = false; break; }
                }
                if (isNew) {
                    clients_.push_back(ClientEndpoint{ ip, port });
                    clientJoined_ = true; // 收到 Join 后游戏正式开始
                    printf("[Server] 客户端已加入: %s:%u（玩家 ID: %d）\n",
                           ip.c_str(), (unsigned)port, pid);
                    fflush(stdout);
                    // 立即广播一次，让客户端尽快拿到初始世界状态
                    BroadcastState();
                }
            }
        } else if (len > 0 && buf[0] == (unsigned char)MsgType::Input) {
            InputPayload p;
            if (NetworkMessage::DeserializeInput(buf, len, p)) {
                ApplyInput(p);
            }
        }
        // 尝试读取下一条（非阻塞，无数据即返回 0 退出循环）
        len = socket_.RecvFrom(buf, sizeof(buf), ip, port);
    }
}
