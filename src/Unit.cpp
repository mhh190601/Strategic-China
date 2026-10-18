#include "Unit.h"

Unit::Unit(float px, float py, float r, SDL_Color c, int t, float h, float s)
    : x(px), y(py), radius(r), color(c), team(t), health(h), maxHealth(h), speed(s) {
}

void Unit::SetTarget(float tx, float ty) {
    hasTarget = true;
    targetX = tx;
    targetY = ty;
}

void Unit::ClearTarget() {
    hasTarget = false;
}

void Unit::SetAttackTarget(int index) {
    attackTargetIndex = index;
    hasTarget = false; // 设置攻击目标时清除移动目标
}

void Unit::ClearAttackTarget() {
    attackTargetIndex = -1;
}
