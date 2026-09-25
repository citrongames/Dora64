#include <SDL.h>
#include <SDL_vulkan.h>

int SDL_main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Dora64",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (window == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    unsigned int extension_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extension_count, nullptr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_Vulkan_GetInstanceExtensions: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_Log("Dora64 Android probe: Vulkan surface extensions: %u", extension_count);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }
        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
