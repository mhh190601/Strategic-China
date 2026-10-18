#pragma once

#include <vector>
#include <string>

// 网络消息类型
enum class MsgType : unsigned char {
    Invalid = 0,
    Input = 1,   // 客户端 -> 主机：玩家输入（选择/移动/攻击）
    State = 2,   // 主机 -> 客户端：完整游戏状态广播
    Join = 3     // 客户端 -> 主机：加入请求（告知主机本机玩家 ID / 队伍）
};

// 操作子类型（仅用于 Input 消息）
enum class InputOp : unsigned char {
    Select = 0,        // 选择单位（byUnitId 有效）
    Move = 1,          // 设置移动目标（byUnitId + x/y 有效）
    Attack = 2         // 设置攻击目标（byUnitId + targetUnitId 有效）
};

// 单位状态快照（用于 State 广播）
struct UnitSnapshot {
    int id = -1;
    float x = 0.0f;
    float y = 0.0f;
    float radius = 16.0f;
    int team = 0;
    float health = 100.0f;
    bool isAlive = true;
};

// 障碍物快照（用于 State 广播，让客户端也能画出与主机一致的地图）
struct ObstacleSnapshot {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// 玩家输入消息负载
struct InputPayload {
    int playerId = 0;                 // 发送方玩家 ID（主机据此决定控制哪一方）
    InputOp op = InputOp::Select;
    int byUnitId = -1;                // 被操作的己方单位 id
    float x = 0.0f;                   // 移动目标 X（Move 用）
    float y = 0.0f;                   // 移动目标 Y（Move 用）
    int targetUnitId = -1;            // 攻击目标单位 id（Attack 用）
};

// 一组网络消息的序列化/反序列化工具（简单二进制格式：[MsgType][payload...]）
namespace NetworkMessage {

    // ---- 输入消息 ----
    std::vector<unsigned char> SerializeInput(const InputPayload& p);
    bool DeserializeInput(const unsigned char* data, int len, InputPayload& out);

    // ---- 状态消息 ----
    // 格式：[MsgType][unitCount:int][UnitSnapshot * N][obstacleCount:int][ObstacleSnapshot * M][winResult:int]
    // winResult 由主机权威判定（0 继续 / 1 主机胜 / 2 主机败），客户端据此同步结束
    std::vector<unsigned char> SerializeState(const std::vector<UnitSnapshot>& units,
                                              const std::vector<ObstacleSnapshot>& obstacles,
                                              int winResult);
    // outObstacles / outWinResult 可传 nullptr（忽略对应字段，兼容短格式）
    bool DeserializeState(const unsigned char* data, int len,
                          std::vector<UnitSnapshot>& outUnits,
                          std::vector<ObstacleSnapshot>* outObstacles = nullptr,
                          int* outWinResult = nullptr);

    // ---- 加入消息（复用 Input 结构，op 字段忽略，只用 playerId）----
    std::vector<unsigned char> SerializeJoin(int playerId);
    bool DeserializeJoin(const unsigned char* data, int len, int& playerId);

} // namespace NetworkMessage
