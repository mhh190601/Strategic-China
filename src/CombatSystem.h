#pragma once

#include <vector>
#include "Unit.h"

// 战斗系统：处理所有单位的攻击逻辑与死亡判定
class CombatSystem {
public:
    // 更新所有单位的攻击（在 World 更新循环中调用）
    static void Update(float deltaTime, std::vector<Unit>& units);
};
