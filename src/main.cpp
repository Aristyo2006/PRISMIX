#include <SDL.h>
#include <iostream>
#include <string>
#include "lvgl.h"
#include "app/app.h"

int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    float zoom = 1.0f;
    // Auto-detect zoom
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        if (dm.w < 1920 || dm.h < 1080) {
            zoom = std::min((float)dm.w / 1920.0f, (float)dm.h / 1080.0f) * 0.9f;
        }
    }

    // Parse args
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--zoom" && i + 1 < argc) {
            zoom = std::stof(argv[++i]);
        }
    }

    lv_init();

    // Create SDL window via LVGL
    lv_display_t* disp = lv_sdl_window_create(1920, 1080);
    if (disp == nullptr) {
        std::cerr << "LVGL SDL display creation failed" << std::endl;
        SDL_Quit();
        return 2;
    }
    lv_display_set_default(disp);
    lv_sdl_window_set_zoom(disp, zoom);
    lv_sdl_window_set_title(disp, "AudioCore UI Simulator");

    // Initialize mouse
    lv_indev_t* mouse = lv_sdl_mouse_create();
    if (mouse == nullptr) {
        std::cerr << "LVGL SDL mouse input creation failed" << std::endl;
        SDL_Quit();
        return 3;
    }

    app_init();

    while (true) {
        uint32_t time_till_next = lv_timer_handler();
        if (time_till_next == 0) time_till_next = 1;
        SDL_Delay(time_till_next);
    }

    return 0;
}
