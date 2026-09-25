#pragma once
#include <filesystem>
#include <SDL.h>
#include "doraemon_touch_state.hpp"

namespace doraemon::touch {
void initialize(const std::filesystem::path& directory);
void process_event(const SDL_Event& event);
void update();
void cancel();
bool is_editing();
GameInput read_input();
CameraInput take_camera();
void draw();
void draw_settings();
}
