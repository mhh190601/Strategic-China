#pragma once

#include <SDL3/SDL.h>

// 游戏单位：表示一个可移动、可被选中的实体
struct Unit {
    float x = 0.0f;                       // 位置 X（单位中心点）
    float y = 0.0f;                       // 位置 Y（单位中心点）
    float radius = 16.0f;                 // 半径（碰撞 / 选中范围）
    SDL_Color color = { 255, 255, 255, 255 }; // 单位颜色（区分阵营）
    int team = 0;                         // 队伍编号（0=玩家，1=敌人）
    int id = -1;                          // 单位的稳定唯一标识（用于网络同步，按创建顺序分配）
    float health = 100.0f;                // 当前血量
    float maxHealth = 100.0f;             // 最大血量（后续画血条用）
    float speed = 200.0f;                 // 移动速度（像素/秒）
    bool hasTarget = false;               // 是否有移动目标
    float targetX = 0.0f;                 // 目标位置 X
    float targetY = 0.0f;                 // 目标位置 Y

    // 战斗属性
    float attackDamage = 20.0f;           // 攻击力
    float attackRange = 80.0f;            // 攻击范围（像素）
    float attackCooldown = 1.0f;          // 攻击冷却时间（秒）
    float attackTimer = 0.0f;             // 当前冷却计时（初始 0，表示可立即攻击）
    int attackTargetIndex = -1;           // 当前攻击目标在单位数组中的索引，-1 表示无
    bool isAlive = true;                  // 是否存活

    Unit() = default;

    // 构造函数：指定位置、半径、颜色、队伍、血量、速度
    Unit(float px, float py, float r, SDL_Color c, int t, float h, float s);

    // 设置移动目标
    void SetTarget(float tx, float ty);

    // 清除移动目标
    void ClearTarget();

    // 设置攻击目标（按索引），并清除移动目标
    void SetAttackTarget(int index);

    // 清除攻击目标
    void ClearAttackTarget();
};
