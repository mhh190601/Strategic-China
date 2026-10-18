#pragma once

#include <vector>
#include "Unit.h"

// AI 控制器：为敌方单位（team == 1）实现简单行为（追击 / 攻击）
class AIController {
public:
    // 更新所有敌方单位的 AI（在 World 更新循环中调用）
    static void Update(float deltaTime, std::vector<Unit>& units);
};
