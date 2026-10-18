#include "NetworkMessage.h"

#include <cstring>
#include <cstdint>

namespace NetworkMessage {

// 小工具：以"小端"写入/读取基础类型（x86 本就小端，这里直接 memcpy 保证可移植）
template <typename T>
static void WriteInt(std::vector<unsigned char>& buf, T v) {
    unsigned char bytes[sizeof(T)];
    std::memcpy(bytes, &v, sizeof(T));
    buf.insert(buf.end(), bytes, bytes + sizeof(T));
}

template <typename T>
static bool ReadInt(const unsigned char*& p, const unsigned char* end, T& out) {
    if (p + sizeof(T) > end) return false;
    std::memcpy(&out, p, sizeof(T));
    p += sizeof(T);
    return true;
}

// ---------------------- 输入消息 ----------------------
std::vector<unsigned char> SerializeInput(const InputPayload& p) {
    std::vector<unsigned char> buf;
    buf.push_back((unsigned char)MsgType::Input);
    WriteInt(buf, p.playerId);
    buf.push_back((unsigned char)p.op);
    WriteInt(buf, p.byUnitId);
    WriteInt(buf, p.x);
    WriteInt(buf, p.y);
    WriteInt(buf, p.targetUnitId);
    return buf;
}

bool DeserializeInput(const unsigned char* data, int len, InputPayload& out) {
    const unsigned char* p = data;
    const unsigned char* end = data + len;
    if (p >= end || (MsgType)*p != MsgType::Input) return false;
    ++p;
    if (!ReadInt(p, end, out.playerId)) return false;
    if (p >= end) return false;
    out.op = (InputOp)*p; ++p;
    if (!ReadInt(p, end, out.byUnitId)) return false;
    if (!ReadInt(p, end, out.x)) return false;
    if (!ReadInt(p, end, out.y)) return false;
    if (!ReadInt(p, end, out.targetUnitId)) return false;
    return true;
}

// ---------------------- 状态消息 ----------------------
std::vector<unsigned char> SerializeState(const std::vector<UnitSnapshot>& units,
                                           const std::vector<ObstacleSnapshot>& obstacles,
                                           int winResult) {
    std::vector<unsigned char> buf;
    buf.push_back((unsigned char)MsgType::State);
    int count = (int)units.size();
    WriteInt(buf, count);
    for (const auto& u : units) {
        WriteInt(buf, u.id);
        WriteInt(buf, u.x);
        WriteInt(buf, u.y);
        WriteInt(buf, u.radius);
        WriteInt(buf, u.team);
        WriteInt(buf, u.health);
        buf.push_back(u.isAlive ? 1 : 0);
    }
    // 追加障碍物段（客户端据此绘制与主机一致的地图）
    int obCount = (int)obstacles.size();
    WriteInt(buf, obCount);
    for (const auto& o : obstacles) {
        WriteInt(buf, o.x);
        WriteInt(buf, o.y);
        WriteInt(buf, o.w);
        WriteInt(buf, o.h);
    }
    // 追加主机判定的胜负结果
    WriteInt(buf, winResult);
    return buf;
}

bool DeserializeState(const unsigned char* data, int len,
                      std::vector<UnitSnapshot>& outUnits,
                      std::vector<ObstacleSnapshot>* outObstacles,
                      int* outWinResult) {
    const unsigned char* p = data;
    const unsigned char* end = data + len;
    if (p >= end || (MsgType)*p != MsgType::State) return false;
    ++p;
    int count = 0;
    if (!ReadInt(p, end, count)) return false;
    if (count < 0 || count > 1000) return false; // 防御异常值
    outUnits.clear();
    outUnits.reserve(count);
    for (int i = 0; i < count; ++i) {
        UnitSnapshot s;
        if (!ReadInt(p, end, s.id)) return false;
        if (!ReadInt(p, end, s.x)) return false;
        if (!ReadInt(p, end, s.y)) return false;
        if (!ReadInt(p, end, s.radius)) return false;
        if (!ReadInt(p, end, s.team)) return false;
        if (!ReadInt(p, end, s.health)) return false;
        if (p >= end) return false;
        s.isAlive = (*p != 0); ++p;
        outUnits.push_back(s);
    }

    // 不关心障碍物时直接结束（尾部的障碍物段被忽略）
    if (!outObstacles) return true;

    int obCount = 0;
    if (!ReadInt(p, end, obCount)) return true; // 兼容旧格式：没有障碍物段
    if (obCount < 0 || obCount > 1000) return true;
    outObstacles->clear();
    outObstacles->reserve(obCount);
    for (int i = 0; i < obCount; ++i) {
        ObstacleSnapshot o;
        if (!ReadInt(p, end, o.x)) return true;
        if (!ReadInt(p, end, o.y)) return true;
        if (!ReadInt(p, end, o.w)) return true;
        if (!ReadInt(p, end, o.h)) return true;
        outObstacles->push_back(o);
    }

    // 读取主机下发的胜负结果（兼容旧格式：没有该字段时按"继续"处理）
    if (!outWinResult) return true;
    int win = 0;
    if (!ReadInt(p, end, win)) { *outWinResult = 0; return true; }
    *outWinResult = win;
    return true;
}

// ---------------------- 加入消息 ----------------------
std::vector<unsigned char> SerializeJoin(int playerId) {
    std::vector<unsigned char> buf;
    buf.push_back((unsigned char)MsgType::Join);
    WriteInt(buf, playerId);
    return buf;
}

bool DeserializeJoin(const unsigned char* data, int len, int& playerId) {
    const unsigned char* p = data;
    const unsigned char* end = data + len;
    if (p >= end || (MsgType)*p != MsgType::Join) return false;
    ++p;
    return ReadInt(p, end, playerId);
}

} // namespace NetworkMessage
