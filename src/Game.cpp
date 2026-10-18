#include "Game.h"
#include <cmath>
#include <algorithm>

// 窗口尺寸
static constexpr int WINDOW_WIDTH = 800;
static constexpr int WINDOW_HEIGHT = 600;

// 固定逻辑步长（60Hz）
static constexpr double FIXED_DT = 1.0 / 60.0;
// 防止"死亡螺旋"：单帧最多累积的逻辑时间
static constexpr double MAX_FRAME_TIME = 0.25;

Game::~Game() {
    if (server_) { delete server_; server_ = nullptr; }
    if (client_) { delete client_; client_ = nullptr; }
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool Game::Init() {
    // 1. 初始化 SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL 初始化失败: %s", SDL_GetError());
        return false;
    }

    // 2. 创建窗口
    window_ = SDL_CreateWindow("2D 策略对战", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_Log("窗口创建失败: %s", SDL_GetError());
        return false;
    }

    // 3. 创建硬件加速渲染器
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_Log("渲染器创建失败: %s", SDL_GetError());
        return false;
    }

    // 4. 单机模式布置初始场景（主机/客户端由 SetMode 另行初始化）
    if (mode_ == Mode::Local) {
        SetupInitialScene();
    }

    return true;
}

void Game::SetMode(const std::string& mode, const std::string& hostIp) {
    if (mode == "host") {
        mode_ = Mode::Host;
        server_ = new Server();
        server_->Start();
    } else if (mode == "client") {
        mode_ = Mode::Client;
        client_ = new Client();
        client_->Connect(hostIp);
    } else {
        mode_ = Mode::Local;
    }
}

void Game::Run() {
    running_ = true;
    double last = SDL_GetTicks() / 1000.0;
    double accumulator = 0.0;

    while (running_) {
        double now = SDL_GetTicks() / 1000.0;
        double frameTime = now - last;
        last = now;
        if (frameTime > MAX_FRAME_TIME) frameTime = MAX_FRAME_TIME;
        accumulator += frameTime;

        // 输入事件（每帧处理，模式选择也在其中）
        HandleEvents();

        // 固定 60Hz 逻辑更新
        // 单机：结束后冻结逻辑，只渲染；
        // 联机（主机/客户端）：结束后仍需继续 Update，以便持续收发状态——
        //   主机把"最终清场状态"广播出去，客户端继续接收并刷新画面，避免残留单位
        while (accumulator >= FIXED_DT) {
            bool freeze = (gameState_ == GameState::GameOver && mode_ == Mode::Local);
            if (!freeze) {
                Update((float)FIXED_DT);
            }
            accumulator -= FIXED_DT;
        }

        Render();
    }
}

void Game::HandleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running_ = false;
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.key == SDLK_R && gameState_ == GameState::GameOver && mode_ == Mode::Local) {
                ResetGame();
            }
            // 模式选择（仅在尚未开始具体模式时有效；此处允许开场时按 H/C 切换）
            // 注意：实际模式在 main 中按首次按键决定，这里保留本地重开快捷键
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            // 已分胜负：不再响应任何操作（联机模式暂不支持重开）
            if (gameState_ == GameState::GameOver) continue;

            float px = event.button.x;
            float py = event.button.y;
            bool right = (event.button.button == SDL_BUTTON_RIGHT);

            if (mode_ == Mode::Local) {
                HandleLocalClick(px, py, right);
            } else if (mode_ == Mode::Host) {
                // 客户端未加入前不响应操作（世界处于冻结等待状态）
                if (server_->IsClientJoined()) HandleLocalClick(px, py, right);
            } else if (mode_ == Mode::Client) {
                // 未收到主机状态前本地没有任何单位，无需处理点击
                // 客户端：根据点击构造输入并发送给主机
                int hit = -1;
                const auto& us = client_->GetLocalUnits();
                for (int i = 0; i < (int)us.size(); ++i) {
                    if (!us[i].isAlive) continue;
                    float dx = us[i].x - px, dy = us[i].y - py;
                    if (sqrtf(dx * dx + dy * dy) <= us[i].radius) { hit = us[i].id; break; }
                }
                if (hit >= 0) {
                    if (right) {
                        client_->SendInput(BuildInputFromClick(InputOp::Move, hit, px, py));
                    } else {
                        // 左键：先选中（记录本地 selectedIndex 对应的 id），再点另一单位设攻击
                        // 简化：左键点己方单位=选中；左键点敌方单位=对当前选中单位设攻击目标
                        if (selectedIndex_ >= 0 && selectedIndex_ < (int)us.size()
                            && us[selectedIndex_].id != hit) {
                            client_->SendInput(BuildInputFromClick(InputOp::Attack, us[selectedIndex_].id, px, py, hit));
                        } else {
                            // 选中本地点（用 id 记录）
                            for (int i = 0; i < (int)us.size(); ++i) {
                                if (us[i].id == hit) { selectedIndex_ = i; break; }
                            }
                        }
                    }
                }
            }
        }
    }
}

InputPayload Game::BuildInputFromClick(InputOp op, int clickUnitId, float x, float y) {
    InputPayload p;
    p.playerId = (mode_ == Mode::Host) ? Server::HOST_PLAYER_ID : Client::CLIENT_PLAYER_ID;
    p.op = op;
    p.byUnitId = clickUnitId;
    p.x = x; p.y = y;
    return p;
}

InputPayload Game::BuildInputFromClick(InputOp op, int clickUnitId, float x, float y, int targetUnitId) {
    InputPayload p = BuildInputFromClick(op, clickUnitId, x, y);
    p.targetUnitId = targetUnitId;
    return p;
}

void Game::HandleLocalClick(float px, float py, bool rightButton) {
    World& world = (mode_ == Mode::Host) ? server_->GetWorld() : world_;
    int hit = PickUnitOnWorld(world, px, py);
    if (hit < 0) return;

    Unit& clicked = world.GetUnits()[hit];
    if (rightButton) {
        // 右键：若选中己方单位则设移动目标；否则忽略
        if (selectedIndex_ >= 0 && selectedIndex_ < (int)world.GetUnits().size()) {
            Unit& sel = world.GetUnits()[selectedIndex_];
            if (sel.isAlive && sel.team == 0) {
                sel.ClearAttackTarget();
                sel.SetTarget(px, py);
            }
        }
    } else {
        // 左键：选中己方单位；或把当前选中单位攻击目标设为点击的敌方单位
        if (clicked.team == 0) {
            selectedIndex_ = hit;
        } else if (selectedIndex_ >= 0 && selectedIndex_ < (int)world.GetUnits().size()) {
            Unit& sel = world.GetUnits()[selectedIndex_];
            if (sel.isAlive && sel.team == 0) {
                sel.SetAttackTarget(hit);
            }
        }
    }
}

// 在世界上做点选（按坐标命中单位）
int Game::PickUnitOnWorld(World& world, float px, float py) const {
    const auto& us = world.GetUnits();
    for (int i = 0; i < (int)us.size(); ++i) {
        if (!us[i].isAlive) continue;
        float dx = us[i].x - px, dy = us[i].y - py;
        if (sqrtf(dx * dx + dy * dy) <= us[i].radius) return i;
    }
    return -1;
}

void Game::Update(float dt) {
    if (mode_ == Mode::Local) {
        ControlLocalPlayer(world_, dt);
        std::vector<int> remap;
        world_.Update(dt, &remap);
        SyncSelectedIndex(remap);
        int result = world_.CheckWinCondition();
        if (result != 0) { gameState_ = GameState::GameOver; winner_ = result; }
    } else if (mode_ == Mode::Host) {
        // 主机：必须每帧调用 server_->Update —— 未加入时它在内部依然要收包（检测 Join），
        // 只是冻结世界。若在这里提前 return，主机将永远收不到客户端的 Join。
        // 已开始且未分胜负：先应用本地玩家（team 0）控制，再推进权威世界（含接收远程输入）
        if (server_->IsClientJoined() && gameState_ == GameState::Playing) {
            ControlLocalPlayer(server_->GetWorld(), dt);
        }
        int result = server_->Update(dt);
        if (result != 0) { gameState_ = GameState::GameOver; winner_ = result; }
    } else if (mode_ == Mode::Client) {
        // 客户端：接收服务器状态并更新本地镜像（结束后仍继续接收，保证画面与主机一致地清场）
        client_->ReceiveState();
        // 胜负以主机权威判定为准，客户端本地不再猜测：
        //   主机结果 1（主机胜）→ 本机失败（红叉）
        //   主机结果 2（主机败）→ 本机胜利（绿圈）
        int hostResult = client_->GetWinResult();
        int result = 0;
        if (hostResult == 1) result = 2;
        else if (hostResult == 2) result = 1;
        if (result != 0) { gameState_ = GameState::GameOver; winner_ = result; }
    }
}

void Game::Render() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    if (mode_ == Mode::Local) {
        world_.Render(renderer_, selectedIndex_);
    } else if (mode_ == Mode::Host) {
        server_->Render(renderer_, selectedIndex_);
    } else if (mode_ == Mode::Client) {
        client_->Render(renderer_, selectedIndex_);
    }

    if (gameState_ == GameState::GameOver) {
        RenderGameOver();
    }

    // 联机等待界面：主机等待客户端加入 / 客户端等待主机状态
    bool waiting = IsWaitingForNetwork();
    if (waiting) {
        RenderWaitingPanel();
    }
    // 等待状态变化时同步更新窗口标题（未集成 SDL_ttf，用标题传达提示文字）
    if (waiting != lastWaiting_) {
        lastWaiting_ = waiting;
        if (waiting) {
            SDL_SetWindowTitle(window_, (mode_ == Mode::Host)
                ? "2D 策略对战 - 主机：等待客户端加入..."
                : "2D 策略对战 - 客户端：等待主机状态...");
        } else {
            SDL_SetWindowTitle(window_, (mode_ == Mode::Host)
                ? "2D 策略对战 - 主机（对战中）"
                : "2D 策略对战 - 客户端（对战中）");
        }
    }

    SDL_RenderPresent(renderer_);
}

// 是否处于"等待联机对方"的状态
bool Game::IsWaitingForNetwork() const {
    if (mode_ == Mode::Host) return server_ && !server_->IsClientJoined();
    if (mode_ == Mode::Client) return client_ && !client_->HasJoined();
    return false; // 单机模式立即开始，不受影响
}

// 等待界面：居中面板 + 三个依次闪烁的圆点表示"等待中"
void Game::RenderWaitingPanel() {
    // 半透明遮罩，压暗底层场景
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_FRect overlay = { 0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT };
    SDL_RenderFillRect(renderer_, &overlay);

    // 居中面板（400 x 160）
    const float panelW = 400.0f;
    const float panelH = 160.0f;
    const float panelX = (WINDOW_WIDTH - panelW) / 2.0f;
    const float panelY = (WINDOW_HEIGHT - panelH) / 2.0f;
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 220);
    SDL_FRect panel = { panelX, panelY, panelW, panelH };
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    SDL_RenderRect(renderer_, &panel);

    // 三个圆点：每 300ms 依次点亮，形成"载入中"的动画效果
    const float cx = panelX + panelW / 2.0f;
    const float cy = panelY + panelH / 2.0f;
    int phase = (int)((SDL_GetTicks() / 300) % 3);
    for (int i = 0; i < 3; ++i) {
        SDL_Color c = (i == phase) ? SDL_Color{ 255, 255, 255, 255 }
                                   : SDL_Color{ 90, 90, 90, 255 };
        DrawFillCircle(renderer_, cx + (i - 1) * 36.0f, cy, 10.0f, c);
    }
}

// ----------------------- 单机逻辑（原阶段 1~5） -----------------------
void Game::SetupInitialScene() {
    // 布置初始单位：玩家阵营（蓝）3 个，敌人阵营（红）2 个
    const SDL_Color blue = { 60, 120, 255, 255 };
    const SDL_Color red  = { 255, 60, 60, 255 };
    Unit u1(220.0f, 180.0f, 22.0f, blue, 0, 100.0f, 220.0f);
    u1.attackDamage = 20.0f; u1.attackRange = 80.0f; u1.attackCooldown = 1.0f;
    Unit u2(340.0f, 260.0f, 22.0f, blue, 0, 100.0f, 220.0f);
    u2.attackDamage = 20.0f; u2.attackRange = 80.0f; u2.attackCooldown = 1.0f;
    Unit u3(240.0f, 360.0f, 22.0f, blue, 0, 100.0f, 220.0f);
    u3.attackDamage = 20.0f; u3.attackRange = 80.0f; u3.attackCooldown = 1.0f;
    Unit u4(600.0f, 200.0f, 24.0f, red, 1, 100.0f, 160.0f);
    u4.attackDamage = 15.0f; u4.attackRange = 70.0f; u4.attackCooldown = 1.2f;
    Unit u5(620.0f, 420.0f, 24.0f, red, 1, 100.0f, 160.0f);
    u5.attackDamage = 15.0f; u5.attackRange = 70.0f; u5.attackCooldown = 1.2f;
    world_.AddUnit(u1);
    world_.AddUnit(u2);
    world_.AddUnit(u3);
    world_.AddUnit(u4);
    world_.AddUnit(u5);

    world_.AddObstacle(Obstacle(300.0f, 280.0f, 200.0f, 50.0f));
    world_.AddObstacle(Obstacle(80.0f, 80.0f, 120.0f, 30.0f));
    world_.AddObstacle(Obstacle(560.0f, 460.0f, 30.0f, 120.0f));
    world_.AddObstacle(Obstacle(400.0f, 80.0f, 30.0f, 90.0f));
}

void Game::ResetGame() {
    world_.ClearUnits();
    SetupInitialScene();
    selectedIndex_ = -1;
    gameState_ = GameState::Playing;
    winner_ = 0;
}

void Game::ControlLocalPlayer(World& world, float dt) {
    // 键盘控制：方向键 / WASD 移动当前选中单位（仅 team 0）
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float dirX = 0.0f, dirY = 0.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) dirY -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) dirY += 1.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) dirX -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dirX += 1.0f;
    float len = sqrtf(dirX * dirX + dirY * dirY);
    bool keyboardActive = (len > 0.0f);
    if (keyboardActive) {
        dirX /= len; dirY /= len;
        auto& us = world.GetUnits();
        if (selectedIndex_ >= 0 && selectedIndex_ < (int)us.size()) {
            Unit& sel = us[selectedIndex_];
            if (sel.isAlive && sel.team == 0) {
                sel.x += dirX * sel.speed * dt;
                sel.y += dirY * sel.speed * dt;
                sel.x = std::max(sel.radius, std::min(World::WORLD_W - sel.radius, sel.x));
                sel.y = std::max(sel.radius, std::min(World::WORLD_H - sel.radius, sel.y));
                sel.ClearTarget();
            }
        }
    }

    // 玩家控制单位自动追击已锁定的攻击目标（键盘未操作时）
    auto& units = world.GetUnits();
    if (!keyboardActive) {
        for (auto& u : units) {
            if (!u.isAlive || u.team != 0) continue;
            if (u.attackTargetIndex < 0 || u.attackTargetIndex >= (int)units.size()) continue;
            Unit& target = units[u.attackTargetIndex];
            if (!target.isAlive) continue;
            float dx = target.x - u.x, dy = target.y - u.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > u.attackRange) u.SetTarget(target.x, target.y);
            else u.ClearTarget();
        }
    }
}

void Game::SyncSelectedIndex(const std::vector<int>& remap) {
    if (selectedIndex_ >= 0 && selectedIndex_ < (int)remap.size()) {
        int ns = remap[selectedIndex_];
        selectedIndex_ = (ns >= 0) ? ns : -1;
    } else if (selectedIndex_ >= 0) {
        selectedIndex_ = -1;
    }
}

void Game::RenderGameOver() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 160);
    SDL_FRect overlay = { 0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT };
    SDL_RenderFillRect(renderer_, &overlay);

    const float panelW = 400.0f, panelH = 200.0f;
    const float panelX = (WINDOW_WIDTH - panelW) / 2.0f;
    const float panelY = (WINDOW_HEIGHT - panelH) / 2.0f;

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 220);
    SDL_FRect panel = { panelX, panelY, panelW, panelH };
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    SDL_RenderRect(renderer_, &panel);

    const float cx = panelX + panelW / 2.0f;
    const float cy = panelY + panelH / 2.0f;

    if (winner_ == 1) {
        SDL_Color green = { 60, 220, 60, 255 };
        DrawFillCircle(renderer_, cx, cy - 20.0f, 45.0f, green);
    } else if (winner_ == 2) {
        float half = 40.0f, thick = 10.0f;
        SDL_SetRenderDrawColor(renderer_, 230, 50, 50, 255);
        SDL_FRect r1 = { cx - half, cy - 20.0f - half, thick, 2.0f * half };
        SDL_FRect r2 = { cx + half - thick, cy - 20.0f - half, thick, 2.0f * half };
        SDL_FRect r3 = { cx - half, cy - 20.0f - half, 2.0f * half, thick };
        SDL_FRect r4 = { cx - half, cy - 20.0f + half - thick, 2.0f * half, thick };
        SDL_RenderFillRect(renderer_, &r1);
        SDL_RenderFillRect(renderer_, &r2);
        SDL_RenderFillRect(renderer_, &r3);
        SDL_RenderFillRect(renderer_, &r4);
    }

    SDL_SetRenderDrawColor(renderer_, 255, 220, 80, 255);
    SDL_FRect hint = { panelX + 60.0f, panelY + panelH - 36.0f, panelW - 120.0f, 6.0f };
    SDL_RenderFillRect(renderer_, &hint);
}
