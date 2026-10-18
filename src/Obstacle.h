#pragma once

#include <SDL3/SDL.h>

// 障碍物：矩形阻挡物（如墙壁、石块）
struct Obstacle {
    float x = 0.0f;                  // 左上角 X
    float y = 0.0f;                  // 左上角 Y
    float w = 0.0f;                  // 宽度
    float h = 0.0f;                  // 高度
    SDL_Color color = { 120, 120, 120, 255 }; // 灰色

    Obstacle() = default;

    // 构造函数：指定位置、尺寸、颜色
    Obstacle(float px, float py, float pw, float ph,
             SDL_Color c = { 120, 120, 120, 255 })
        : x(px), y(py), w(pw), h(ph), color(c) {
    }

    // 判断点 (px, py) 是否落在矩形内
    bool Contains(float px, float py) const;
};
