#include "src/Game.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 让控制台以 UTF-8 输出，保证中文调试日志（[Server]/[Client]）不乱码
    SetConsoleOutputCP(CP_UTF8);
#endif
    // 联机测试默认主机 IP（客户端连接用）。如需连真实机器，可改用配置文件或命令行参数。
    std::string hostIp = "127.0.0.1";

    // 允许通过命令行指定模式与主机 IP，便于快速测试：
    //   MyGame.exe host
    //   MyGame.exe client 192.168.1.10
    std::string mode = "local";
    if (argc >= 2) mode = argv[1];
    if (argc >= 3) hostIp = argv[2];

    Game game;
    if (!game.Init()) {
        std::cerr << "初始化失败" << std::endl;
        return 1;
    }

    // 若命令行已指定模式，直接进入；否则等待玩家按 H（主机）/ C（客户端）选择，
    // 不按则默认单机模式（任意其它键或超时也进入单机）。
    if (mode == "host" || mode == "client") {
        if (mode == "host") {
            std::cout << "[提示] 主机模式：等待客户端加入后才会开始游戏（世界处于冻结状态）" << std::endl;
        } else {
            std::cout << "[提示] 客户端模式：正在连接主机 " << hostIp << ":12345 ..." << std::endl;
        }
        game.SetMode(mode, hostIp);
    } else {
        std::cout << "=== 2D 策略对战 ===" << std::endl;
        std::cout << "按 H 创建主机（联机房主）" << std::endl;
        std::cout << "按 C 加入主机（联机客户端，默认 127.0.0.1）" << std::endl;
        std::cout << "按其它任意键 / 直接运行 进入单机模式" << std::endl;

        SDL_Event ev;
        bool chosen = false;
        while (!chosen && SDL_WaitEvent(&ev)) {
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_H) { game.SetMode("host", hostIp); }
                else if (ev.key.key == SDLK_C) { game.SetMode("client", hostIp); }
                else { game.SetMode("local"); }
                chosen = true;
            } else if (ev.type == SDL_EVENT_QUIT) {
                return 0;
            }
        }
    }

    game.Run();
    return 0;
}
