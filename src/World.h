#pragma once

#include <vector>
#include <SDL3/SDL.h>
#include "Unit.h"
#include "Obstacle.h"

// 用三角形扇形近似绘制实心圆（SDL3 未提供圆绘制 API），在 Game 渲染结束界面时也会用到
void DrawFillCircle(SDL_Renderer* renderer, float cx, float cy, float r, SDL_Color color);

// 游戏世界：管理所有单位与障碍物，负责逻辑更新、碰撞与渲染
class World {
public:
    World() = default;

    // 添加一个单位
    void AddUnit(const Unit& unit);

    // 添加一个障碍物
    void AddObstacle(const Obstacle& obstacle);

    // 更新所有单位的逻辑（目标移动），随后做一次碰撞处理
    // outRemap 可选：接收"旧索引 -> 新索引"映射（死亡单位映射 -1），供外部同步选中索引
    void Update(float dt, std::vector<int>* outRemap = nullptr);

    // 单独执行碰撞处理（单位间 + 单位与障碍），保持单位不重叠、不穿墙
    void ResolveCollisions();

    // 渲染障碍物与所有单位；selectedIndex 为选中的单位索引，-1 表示无选中
    void Render(SDL_Renderer* renderer, int selectedIndex) const;

    // 获取单位列表（可修改）
    std::vector<Unit>& GetUnits();

    // 获取单位列表（只读）
    const std::vector<Unit>& GetUnits() const;

    // 按稳定 id 查找单位，找到返回指针，否则返回 nullptr（网络同步用）
    Unit* FindUnitById(int id);

    // 获取障碍物列表（只读）
    const std::vector<Obstacle>& GetObstacles() const;

    // 胜负判定：扫描所有存活单位
    //   返回 0：游戏继续（双方均有存活单位）
    //   返回 1：玩家胜利（所有敌方单位死亡）
    //   返回 2：玩家失败（所有玩家单位死亡）
    int CheckWinCondition() const;

    // 清空所有单位（用于重新开始游戏，障碍物保留）
    void ClearUnits();

    // 判断目标点是否落在某个障碍物内部（右键点障碍时不设目标）
    bool IsPointInObstacle(float px, float py) const;

    // 将目标点调整到障碍物外最近的可达位置（在障碍物内时退回边缘）
    void ClampTargetOutsideObstacles(float& tx, float& ty) const;

    // 窗口尺寸（用于边界限制）
    static constexpr float WORLD_W = 800.0f;
    static constexpr float WORLD_H = 600.0f;

private:
    std::vector<Unit> units_;       // 所有单位
    std::vector<Obstacle> obstacles_; // 所有障碍物
    int nextId_ = 0;                // 单位 id 分配计数器（保证 id 稳定且唯一）

    // 处理单个单位与所有障碍物的碰撞（最小穿透轴推出）
    void ResolveUnitVsObstacles(Unit& unit);
    // 处理任意两个单位之间的圆形碰撞（沿中心连线推开）
    void ResolveUnitVsUnits();

    // 移除所有死亡（isAlive == false）的单位；返回"旧索引 -> 新索引"映射，
    // 新索引为 -1 表示该项已死亡被移除，供外部（Game）同步选中索引
    std::vector<int> RemoveDeadUnits();
};
