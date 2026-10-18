#pragma once

#include "World.h"
#include "Server.h"
#include "Client.h"
#include <SDL3/SDL.h>

// 游戏主类：负责 SDL 初始化、窗口/渲染器、游戏循环与输入处理
// 支持三种模式：单机（Local）、主机（Host，权威服务器）、客户端（Client，联机加入）
class Game {
public:
    Game() = default;
    ~Game();

    // 初始化 SDL、创建窗口与渲染器
    bool Init();

    // 运行主循环（固定 60Hz 逻辑更新，渲染不限帧）
    void Run();

    // 设置运行模式：Local / Host / Client
    void SetMode(const std::string& mode, const std::string& hostIp = "127.0.0.1");

private:
    // 游戏整体状态：进行中 / 已结束
    enum class GameState {
        Playing,   // 进行中
        GameOver   // 已结束
    };

    // 运行模式
    enum class Mode {
        Local,  // 单机
        Host,   // 主机（权威）
        Client  // 客户端（联机）
    };

    SDL_Window* window_ = nullptr;     // 窗口
    SDL_Renderer* renderer_ = nullptr; // 渲染器
    World world_;                      // 单机模式下的游戏世界
    Server* server_ = nullptr;         // 主机模式对象（按需创建）
    Client* client_ = nullptr;         // 客户端模式对象（按需创建）
    Mode mode_ = Mode::Local;          // 当前模式
    int selectedIndex_ = -1;           // 当前选中单位索引/本地镜像索引，-1 表示未选中
    bool running_ = false;             // 是否运行中
    GameState gameState_ = GameState::Playing; // 当前游戏状态，初始为进行中
    int winner_ = 0;                   // 胜利方：0 未决、1 玩家胜利、2 玩家失败
    bool lastWaiting_ = false;         // 上一帧是否处于等待联机状态（用于按需更新窗口标题）

    // 处理输入事件（鼠标点击选中 / 设置目标 / 重开 / 模式选择）
    void HandleEvents();

    // 逻辑更新（固定步长调用）
    void Update(float dt);

    // 渲染一帧
    void Render();

    // ---- 单机模式辅助 ----
    void SetupInitialScene();
    void ResetGame();
    void RenderGameOver();

    // 联机等待界面：主机等待客户端加入 / 客户端等待主机状态
    // 未集成 SDL_ttf，用"面板 + 三个闪烁圆点"表示等待中
    void RenderWaitingPanel();

    // 是否处于"等待联机对方"的状态（此时冻结游戏逻辑）
    bool IsWaitingForNetwork() const;

    // ---- 本地玩家控制（单机与主机共用，操作 team 0）----
    // 键盘移动选中单位 + 自动追击已锁定目标；并在单位数组重排后同步选中索引
    void ControlLocalPlayer(World& world, float dt);
    // 在指定世界上做点选（按坐标命中单位，返回数组索引）
    int PickUnitOnWorld(World& world, float px, float py) const;

    // 单位数组因死亡移除重排后，同步选中索引
    void SyncSelectedIndex(const std::vector<int>& remap);

    // ---- 联机模式辅助 ----
    // 根据点击构造输入负载（客户端 / 主机本地发送用）
    InputPayload BuildInputFromClick(InputOp op, int clickUnitId, float x, float y);
    InputPayload BuildInputFromClick(InputOp op, int clickUnitId, float x, float y, int targetUnitId);

    // 单机/主机下的本地点击处理（左键选择 / 左键设攻击 / 右键移动）
    void HandleLocalClick(float px, float py, bool rightButton);
};
