#include "World.h"
#include <cmath>
#include <algorithm>
#include "CombatSystem.h"
#include "AIController.h"

// 圆周率（MSVC 的 <cmath> 默认不暴露 M_PI，这里自行定义）
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// 用三角形扇形近似绘制实心圆（SDL3 未提供圆绘制 API）
void DrawFillCircle(SDL_Renderer* renderer, float cx, float cy, float r, SDL_Color color) {
    if (r <= 0.0f) return;
    // SDL_Vertex 的颜色字段是 SDL_FColor（分量 0.0~1.0 浮点），需要转换
    const SDL_FColor fc = {
        color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f
    };
    const int segments = 48; // 分片数，越多越平滑
    for (int i = 0; i < segments; ++i) {
        float a0 = (float)i / (float)segments * M_PI * 2.0f;
        float a1 = (float)(i + 1) / (float)segments * M_PI * 2.0f;

        SDL_Vertex v0 = { SDL_FPoint{ cx, cy }, fc, SDL_FPoint{ 0.0f, 0.0f } };
        SDL_Vertex v1 = { SDL_FPoint{ cx + cosf(a0) * r, cy + sinf(a0) * r }, fc, SDL_FPoint{ 0.0f, 0.0f } };
        SDL_Vertex v2 = { SDL_FPoint{ cx + cosf(a1) * r, cy + sinf(a1) * r }, fc, SDL_FPoint{ 0.0f, 0.0f } };

        SDL_Vertex verts[3] = { v0, v1, v2 };
        SDL_RenderGeometry(renderer, nullptr, verts, 3, nullptr, 0);
    }
}

// 用折线绘制空心圆（选中高亮）
void DrawCircle(SDL_Renderer* renderer, float cx, float cy, float r) {
    if (r <= 0.0f) return;
    const int segments = 48;
    SDL_FPoint points[49];
    for (int i = 0; i < segments; ++i) {
        float a = (float)i / (float)segments * M_PI * 2.0f;
        points[i] = SDL_FPoint{ cx + cosf(a) * r, cy + sinf(a) * r };
    }
    points[segments] = points[0]; // 闭合
    SDL_RenderLines(renderer, points, segments + 1);
}

void World::AddUnit(const Unit& unit) {
    units_.push_back(unit);
    units_.back().id = nextId_++; // 分配稳定唯一 id（网络同步关键）
}

void World::AddObstacle(const Obstacle& obstacle) {
    obstacles_.push_back(obstacle);
}

const std::vector<Unit>& World::GetUnits() const {
    return units_;
}

Unit* World::FindUnitById(int id) {
    for (auto& u : units_) {
        if (u.id == id) return &u;
    }
    return nullptr;
}

std::vector<Unit>& World::GetUnits() {
    return units_;
}

const std::vector<Obstacle>& World::GetObstacles() const {
    return obstacles_;
}

int World::CheckWinCondition() const {
    bool playerAlive = false;  // 玩家阵营（team 0）是否有存活单位
    bool enemyAlive = false;   // 敌方阵营（team 1）是否有存活单位
    for (const auto& u : units_) {
        if (!u.isAlive) continue;
        if (u.team == 0) playerAlive = true;
        else if (u.team == 1) enemyAlive = true;
    }

    if (!enemyAlive && playerAlive) return 1;  // 敌方全灭、玩家尚有存活：玩家胜利
    if (!playerAlive && enemyAlive) return 2;  // 玩家全灭、敌方尚有存活：玩家失败
    return 0;                                  // 否则游戏继续
}

void World::ClearUnits() {
    units_.clear();
}

bool World::IsPointInObstacle(float px, float py) const {
    for (const auto& ob : obstacles_) {
        if (ob.Contains(px, py)) return true;
    }
    return false;
}

void World::ClampTargetOutsideObstacles(float& tx, float& ty) const {
    // 如果目标点落在障碍物内部，沿最近边缘推到障碍物外
    for (const auto& ob : obstacles_) {
        if (ob.Contains(tx, ty)) {
            float left = ob.x;
            float right = ob.x + ob.w;
            float top = ob.y;
            float bottom = ob.y + ob.h;
            // 各方向到边缘的距离
            float dl = tx - left;
            float dr = right - tx;
            float dt = ty - top;
            float db = bottom - ty;
            // 取最小穿透方向推出
            float m = std::min({ dl, dr, dt, db });
            if (m == dl) tx = left;
            else if (m == dr) tx = right;
            else if (m == dt) ty = top;
            else ty = bottom;
        }
    }
}

void World::Update(float dt, std::vector<int>* outRemap) {
    // 1. 简单 AI：敌方单位追击 / 锁定攻击目标（在移动前决策）
    AIController::Update(dt, units_);

    // 2. 单位目标移动（仅存活单位）
    for (auto& unit : units_) {
        if (!unit.isAlive || !unit.hasTarget) continue; // 死亡或无移动目标则不动

        // 计算指向目标的方向向量
        float dx = unit.targetX - unit.x;
        float dy = unit.targetY - unit.y;
        float dist = sqrtf(dx * dx + dy * dy);

        // 距离足够近，视为已到达目标，停止
        if (dist < 1.0f) {
            unit.ClearTarget();
            continue;
        }

        // 本帧可移动的距离 = 速度 x 时间
        float step = unit.speed * dt;
        if (step >= dist) {
            // 一步内可到达：直接走到目标点并停止
            unit.x = unit.targetX;
            unit.y = unit.targetY;
            unit.ClearTarget();
        } else {
            // 沿目标方向移动
            unit.x += (dx / dist) * step;
            unit.y += (dy / dist) * step;
        }
    }

    // 移动之后统一做碰撞处理（单位 vs 障碍、单位 vs 单位）
    ResolveCollisions();

    // 3. 战斗系统：处理所有单位的攻击与死亡判定
    CombatSystem::Update(dt, units_);

    // 4. 清理死亡单位（移除 isAlive == false 的单位），返回索引重映射表
    std::vector<int> remap = RemoveDeadUnits();
    if (outRemap) *outRemap = remap;
}

std::vector<int> World::RemoveDeadUnits() {
    // 先构建"旧索引 -> 新索引"的映射表：仅存活单位保留并紧凑排列
    // 死亡单位映射为 -1，存活单位映射为其在新数组中的位置
    std::vector<int> remap(units_.size(), -1);
    int newIndex = 0;
    for (int i = 0; i < (int)units_.size(); ++i) {
        if (units_[i].isAlive) {
            remap[i] = newIndex;
            ++newIndex;
        }
    }

    // 用映射表修正每个存活单位的攻击目标索引，使其指向重排后的正确位置
    for (int i = 0; i < (int)units_.size(); ++i) {
        if (!units_[i].isAlive) continue;
        int& t = units_[i].attackTargetIndex;
        if (t >= 0 && t < (int)remap.size()) {
            if (remap[t] >= 0) {
                t = remap[t];  // 目标仍存活：映射到新索引
            } else {
                units_[i].ClearAttackTarget(); // 目标已死亡：清空攻击目标
            }
        } else if (t >= 0) {
            // 原索引越界：直接清空
            units_[i].ClearAttackTarget();
        }
    }

    // 从数组中移除所有死亡单位（按映射表保留存活单位）
    std::vector<Unit> alive;
    alive.reserve(newIndex);
    for (auto& u : units_) {
        if (u.isAlive) alive.push_back(u);
    }
    units_.swap(alive);

    return remap; // 返回映射表，供 Game 同步选中索引
}

void World::ResolveCollisions() {
    // 1. 单位与障碍物碰撞（最小穿透轴推出），跳过死亡单位
    for (auto& unit : units_) {
        if (!unit.isAlive) continue;
        ResolveUnitVsObstacles(unit);
    }
    // 2. 单位之间的圆形碰撞（相互推开），仅存活单位参与
    ResolveUnitVsUnits();

    // 3. 限制在窗口边界内（存活单位）
    for (auto& unit : units_) {
        if (!unit.isAlive) continue;
        if (unit.x < unit.radius) unit.x = unit.radius;
        if (unit.y < unit.radius) unit.y = unit.radius;
        if (unit.x > WORLD_W - unit.radius) unit.x = WORLD_W - unit.radius;
        if (unit.y > WORLD_H - unit.radius) unit.y = WORLD_H - unit.radius;
    }
}

void World::ResolveUnitVsObstacles(Unit& unit) {
    for (const auto& ob : obstacles_) {
        // 计算矩形上离圆心最近的点（圆-矩形最近点）
        float nearestX = std::max(ob.x, std::min(unit.x, ob.x + ob.w));
        float nearestY = std::max(ob.y, std::min(unit.y, ob.y + ob.h));
        float dx = unit.x - nearestX;
        float dy = unit.y - nearestY;
        float distSq = dx * dx + dy * dy;

        if (distSq > unit.radius * unit.radius) {
            continue; // 不重叠，跳过
        }

        if (distSq > 1e-6f) {
            // 圆心在矩形外：沿圆心到最近点的方向推出
            float dist = sqrtf(distSq);
            float push = unit.radius - dist;
            unit.x += (dx / dist) * push;
            unit.y += (dy / dist) * push;
        } else {
            // 圆心在矩形内部：按最小穿透轴推出（选最近的一边）
            float dl = unit.x - ob.x;            // 到左边缘
            float dr = (ob.x + ob.w) - unit.x;   // 到右边缘
            float dt = unit.y - ob.y;            // 到上边缘
            float db = (ob.y + ob.h) - unit.y;   // 到下边缘
            float m = std::min({ dl, dr, dt, db });
            if (m == dl)       unit.x = ob.x - unit.radius;
            else if (m == dr)  unit.x = ob.x + ob.w + unit.radius;
            else if (m == dt)  unit.y = ob.y - unit.radius;
            else               unit.y = ob.y + ob.h + unit.radius;
        }
    }
}

void World::ResolveUnitVsUnits() {
    const int n = (int)units_.size();
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
        Unit& a = units_[i];
        Unit& b = units_[j];
        if (!a.isAlive || !b.isAlive) continue; // 跳过死亡单位
        float dx = b.x - a.x;
            float dy = b.y - a.y;
            float distSq = dx * dx + dy * dy;
            float minDist = a.radius + b.radius;

            if (distSq >= minDist * minDist) continue; // 不重叠

            float dist = sqrtf(distSq);
            if (dist < 1e-4f) {
                // 完全重合：给一个确定方向避免除零
                dx = 1.0f; dy = 0.0f; dist = 0.0001f;
            }
            float overlap = minDist - dist;
            float nx = dx / dist;
            float ny = dy / dist;
            // 各推一半，互不重叠
            a.x -= nx * overlap * 0.5f;
            a.y -= ny * overlap * 0.5f;
            b.x += nx * overlap * 0.5f;
            b.y += ny * overlap * 0.5f;
        }
    }
}

void World::Render(SDL_Renderer* renderer, int selectedIndex) const {
    // 先绘制障碍物（灰色矩形）
    for (const auto& ob : obstacles_) {
        SDL_FRect rect = { ob.x, ob.y, ob.w, ob.h };
        SDL_SetRenderDrawColor(renderer, ob.color.r, ob.color.g, ob.color.b, ob.color.a);
        SDL_RenderFillRect(renderer, &rect);
    }

    // 再绘制所有单位（彩色圆形）+ 血条 + 选中高亮
    for (int i = 0; i < (int)units_.size(); ++i) {
        const Unit& unit = units_[i];
        if (!unit.isAlive) continue; // 死亡单位不渲染

        DrawFillCircle(renderer, unit.x, unit.y, unit.radius, unit.color);

        // 选中单位：绘制白色圆圈高亮
        if (i == selectedIndex) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            DrawCircle(renderer, unit.x, unit.y, unit.radius + 4.0f);
        }

        // 血条：位于单位头顶上方
        const float barW = unit.radius * 2.0f;   // 血条宽度与直径相关
        const float barH = 5.0f;                  // 血条高度固定
        const float barX = unit.x - unit.radius;
        const float barY = unit.y - unit.radius - barH - 4.0f;

        // 背景（暗红）
        SDL_FRect bg = { barX, barY, barW, barH };
        SDL_SetRenderDrawColor(renderer, 80, 0, 0, 255);
        SDL_RenderFillRect(renderer, &bg);

        // 前景（绿，血量低于 30% 变黄红色）
        float ratio = unit.maxHealth > 0.0f ? (unit.health / unit.maxHealth) : 0.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        SDL_FRect fg = { barX, barY, barW * ratio, barH };
        if (ratio < 0.3f) {
            SDL_SetRenderDrawColor(renderer, 255, 80, 0, 255);  // 低血：橙红
        } else {
            SDL_SetRenderDrawColor(renderer, 0, 220, 0, 255);   // 正常：绿
        }
        SDL_RenderFillRect(renderer, &fg);
    }
}
