// COASTLINE — Фестиваль Побережья. Точка входа.
#include "game/game.h"
#include "raylib.h"

#if defined(PLATFORM_WEB)
#include <emscripten.h>
static void webFrame(void* g) { static_cast<cl::Game*>(g)->frame(); }
#endif

int main(int argc, char** argv) {
    static cl::Game game;  // большой объект — не на стеке
    game.parseArgs(argc, argv);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(game.winW, game.winH, "COASTLINE — Фестиваль Побережья");
    SetExitKey(KEY_NULL);  // Esc — это меню, а не выход
#if !defined(PLATFORM_WEB)
    SetWindowMinSize(800, 450);
#endif
    game.init();
#if defined(PLATFORM_WEB)
    emscripten_set_main_loop_arg(webFrame, &game, 0, 1);
#else
    while (!WindowShouldClose() && !game.quit) game.frame();
    game.shutdown();
    CloseWindow();
#endif
    return 0;
}
