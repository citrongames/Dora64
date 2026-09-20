#pragma once

#include <filesystem>

namespace doraemon::system_overlay {
    // Loads the PC-side settings stored next to the ROM/configuration files.
    void initialize(const std::filesystem::path& config_directory);

    // Used by the N64 input bridge to suppress gameplay input while the PC
    // menu owns the keyboard and mouse.
    bool is_menu_open();
    void toggle_menu();

    // Called by RT64 before ImGui::NewFrame so controller navigation is
    // sampled at the same time as the platform keyboard and mouse input.
    void update_input();

    // Called by RT64 while its GUI frame is active. Add future system HUD
    // elements here so they stay independent from the game's own rendering.
    void draw();

    // Present-thread draw hook; count output frames without accessing ImGui.
    void record_output_frame();
}
