#pragma once

namespace doraemon::camera {
    void configure(bool enabled, float stickSpeed, float mouseSensitivity, bool invertY, bool captureMouse = false, bool invertX = false);
    bool is_active();
    bool is_enabled();
    bool is_dialogue_active();
    bool is_mouse_capture_enabled();
    // Main-thread samples. Mouse deltas are consumed exactly once by a game update.
    void submit_input(float yaw, float pitch, float mouseX, float mouseY, float zoom = 0, float wheel = 0);
    void clear_input();
    void reset();
}
