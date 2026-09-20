#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <SDL.h>

#include "ultramodern/input.hpp"

namespace doraemon::input {
    inline constexpr int MinimumStickDeadZonePercent = 0;
    inline constexpr int MaximumStickDeadZonePercent = 50;
    inline constexpr int DefaultStickDeadZonePercent = 25;

    enum class Action : std::uint8_t {
        MoveUp,
        MoveDown,
        MoveLeft,
        MoveRight,
        JumpConfirm,
        AttackBack,
        Crouch,
        Pause,
        LeftTrigger,
        CameraMode,
        DpadUp,
        DpadDown,
        DpadLeft,
        DpadRight,
        CameraUp,
        CameraDown,
        CameraLeft,
        CameraRight,
        CameraZoomIn,
        CameraZoomOut,
        Count
    };

    enum class GamepadInput : std::uint8_t {
        None,
        A,
        B,
        X,
        Y,
        Start,
        LeftShoulder,
        RightShoulder,
        LeftTrigger,
        RightTrigger,
        LeftStickButton,
        RightStickButton,
        DpadUp,
        DpadDown,
        DpadLeft,
        DpadRight,
        LeftStickUp,
        LeftStickDown,
        LeftStickLeft,
        LeftStickRight,
        RightStickUp,
        RightStickDown,
        RightStickLeft,
        RightStickRight,
        Count
    };

    enum class BindingDevice : std::uint8_t {
        Keyboard,
        Gamepad
    };

    struct ActionBinding {
        SDL_Scancode keyboard = SDL_SCANCODE_UNKNOWN;
        GamepadInput gamepad = GamepadInput::None;
    };

    void initialize(const std::filesystem::path& config_directory);
    void initialize_sdl();
    void shutdown();
    void process_event(const SDL_Event& event);
    void update();
    void poll();
    bool get_input(int controller_num, uint16_t* buttons, float* x, float* y);
    void set_rumble(int controller_num, bool rumble);
    ultramodern::input::connected_device_info_t get_connected_device_info(
        int controller_num);

    const char* action_name(Action action);
    std::string keyboard_binding_name(Action action);
    std::string gamepad_binding_name(Action action);
    ActionBinding get_binding(Action action);
    void set_keyboard_binding(Action action, SDL_Scancode scancode);
    void set_gamepad_binding(Action action, GamepadInput input);
    void reset_bindings();

    void begin_binding_capture(Action action, BindingDevice device);
    void cancel_binding_capture();
    bool is_binding_capture_active();

    bool is_gamepad_connected();
    std::string gamepad_name();
    float gamepad_input_value(GamepadInput input);
    int stick_dead_zone_percent();
    void set_stick_dead_zone_percent(int percent);
    bool is_background_gamepad_input_enabled();
    void set_background_gamepad_input_enabled(bool enabled);
}
