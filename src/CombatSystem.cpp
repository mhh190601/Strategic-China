#include "CombatSystem.h"

void CombatSystem::Update(float deltaTime, std::vector<Unit>& units) {
    const int n = (int)units.size();
    for (int i = 0; i < n; ++i) {
        Unit& attacker = units[i];
        if (!attacker.isAlive) continue; // 死亡单位不攻击

        // 冷却计时递减
        if (attacker.attackTimer > 0.0f) {
            attacker.attackTimer -= deltaTime;
        }

        // 没有攻击目标则跳过
        if (attacker.attackTargetIndex < 0) continue;

        // 目标索引越界或目标已死亡：清空攻击目标
        if (attacker.attackTargetIndex >= n || !units[attacker.attackTargetIndex].isAlive) {
            attacker.ClearAttackTarget();
            continue;
        }

        Unit& target = units[attacker.attackTargetIndex];

        // 计算两者距离（圆心到圆心）
        float dx = target.x - attacker.x;
        float dy = target.y - attacker.y;
        float dist = sqrtf(dx * dx + dy * dy);

        // 目标在攻击范围内且冷却结束：发动攻击
        if (dist <= attacker.attackRange && attacker.attackTimer <= 0.0f) {
            target.health -= attacker.attackDamage;
            attacker.attackTimer = attacker.attackCooldown; // 重置冷却

            // 目标死亡判定
            if (target.health <= 0.0f) {
                target.health = 0.0f;
                target.isAlive = false;
                attacker.ClearAttackTarget(); // 目标已死，清空自身攻击目标
            }
        }
    }
}
