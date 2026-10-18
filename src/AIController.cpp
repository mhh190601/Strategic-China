#include "AIController.h"
#include <cmath>

void AIController::Update(float deltaTime, std::vector<Unit>& units) {
    const int n = (int)units.size();

    for (int i = 0; i < n; ++i) {
        Unit& enemy = units[i];
        if (!enemy.isAlive || enemy.team != 1) continue; // 只处理存活的敌方单位

        // 1. 寻找最近的存活玩家单位（team == 0）
        int nearestIndex = -1;
        float nearestDist = -1.0f;
        for (int j = 0; j < n; ++j) {
            Unit& player = units[j];
            if (!player.isAlive || player.team != 0) continue;
            float dx = player.x - enemy.x;
            float dy = player.y - enemy.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (nearestIndex < 0 || dist < nearestDist) {
                nearestIndex = j;
                nearestDist = dist;
            }
        }

        if (nearestIndex < 0) {
            // 没有存活的玩家单位，停止并清除目标
            enemy.ClearTarget();
            enemy.ClearAttackTarget();
            continue;
        }

        // 2. 若最近玩家在攻击范围内：锁定为攻击目标，停止移动
        if (nearestDist <= enemy.attackRange) {
            enemy.SetAttackTarget(nearestIndex); // 内部会清除移动目标
        } else {
            // 3. 否则：追击玩家（设定移动目标为其位置），清除攻击目标
            enemy.ClearAttackTarget();
            enemy.SetTarget(units[nearestIndex].x, units[nearestIndex].y);
        }
    }
}
