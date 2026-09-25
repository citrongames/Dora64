#include "doraemon_input.hpp"
#include "doraemon_touch.hpp"
#include "system_overlay.hpp"
#include "doraemon_camera.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <mutex>
#include <string>

#include "json/json.hpp"

namespace {
    using doraemon::input::Action;
    using doraemon::input::ActionBinding;
    using doraemon::input::BindingDevice;
    using doraemon::input::GamepadInput;

    constexpr uint16_t N64_A = 0x8000;
    constexpr uint16_t N64_B = 0x4000;
    constexpr uint16_t N64_Z = 0x2000;
    constexpr uint16_t N64_START = 0x1000;
    constexpr uint16_t N64_DPAD_UP = 0x0800;
    constexpr uint16_t N64_DPAD_DOWN = 0x0400;
    constexpr uint16_t N64_DPAD_LEFT = 0x0200;
    constexpr uint16_t N64_DPAD_RIGHT = 0x0100;
    constexpr uint16_t N64_L = 0x0020;
    constexpr uint16_t N64_R = 0x0010;
    constexpr uint16_t N64_C_UP = 0x0008;
    constexpr uint16_t N64_C_DOWN = 0x0004;
    constexpr uint16_t N64_C_LEFT = 0x0002;
    constexpr uint16_t N64_C_RIGHT = 0x0001;

    constexpr std::size_t ActionCount = static_cast<std::size_t>(Action::Count);
    constexpr std::size_t GamepadInputCount = static_cast<std::size_t>(GamepadInput::Count);
    constexpr Sint16 TriggerDeadZone = 4000;

    constexpr std::array<const char*, ActionCount> ActionKeys = {
        "move_up", "move_down", "move_left", "move_right",
        "jump_confirm", "attack_back", "crouch", "pause",
        "left_trigger", "camera_mode",
        "dpad_up", "dpad_down", "dpad_left", "dpad_right",
        "camera_up", "camera_down", "camera_left", "camera_right",
        "camera_zoom_in", "camera_zoom_out"
    };

    constexpr std::array<const char*, ActionCount> ActionNames = {
        "Move forward",
        "Move backward",
        "Move left",
        "Move right",
        "Jump / Confirm (N64 A)",
        "Attack / Back (N64 B)",
        "Crouch / Descend (N64 Z)",
        "Pause / Character menu (Start)",
        "Left trigger (N64 L)",
        "Camera mode (N64 R)",
        "D-pad up / Modern zoom in",
        "D-pad down / Modern zoom out",
        "D-pad left",
        "D-pad right",
        "Camera up / Zoom in",
        "Camera down / Zoom out",
        "Camera left",
        "Camera right",
        "Modern camera zoom in",
        "Modern camera zoom out"
    };

    constexpr std::array<const char*, GamepadInputCount> GamepadInputNames = {
        "Unbound", "A", "B", "X", "Y", "Start", "LB", "RB", "LT", "RT",
        "Left stick click", "Right stick click",
        "D-pad up", "D-pad down", "D-pad left", "D-pad right",
        "Left stick up", "Left stick down", "Left stick left", "Left stick right",
        "Right stick up", "Right stick down", "Right stick left", "Right stick right"
    };

    constexpr std::array<ActionBinding, ActionCount> defaultBindings() {
        return {{
            { SDL_SCANCODE_W, GamepadInput::LeftStickUp },
            { SDL_SCANCODE_S, GamepadInput::LeftStickDown },
            { SDL_SCANCODE_A, GamepadInput::LeftStickLeft },
            { SDL_SCANCODE_D, GamepadInput::LeftStickRight },
            { SDL_SCANCODE_SPACE, GamepadInput::A },
            { SDL_SCANCODE_C, GamepadInput::B },
            { SDL_SCANCODE_Z, GamepadInput::LeftTrigger },
            { SDL_SCANCODE_RETURN, GamepadInput::Start },
            { SDL_SCANCODE_Q, GamepadInput::LeftShoulder },
            { SDL_SCANCODE_E, GamepadInput::RightShoulder },
            { SDL_SCANCODE_UP, GamepadInput::DpadUp },
            { SDL_SCANCODE_DOWN, GamepadInput::DpadDown },
            { SDL_SCANCODE_LEFT, GamepadInput::DpadLeft },
            { SDL_SCANCODE_RIGHT, GamepadInput::DpadRight },
            { SDL_SCANCODE_I, GamepadInput::RightStickUp },
            { SDL_SCANCODE_K, GamepadInput::RightStickDown },
            { SDL_SCANCODE_J, GamepadInput::RightStickLeft },
            { SDL_SCANCODE_L, GamepadInput::RightStickRight },
            { SDL_SCANCODE_PAGEUP, GamepadInput::None },
            { SDL_SCANCODE_PAGEDOWN, GamepadInput::None }
        }};
    }

    std::atomic<uint16_t> currentButtons{0};
    std::atomic<float> currentStickX{0.0f};
    std::atomic<float> currentStickY{0.0f};

    std::atomic_bool controllerConnected{false};
    std::atomic<uint32_t> controllerButtons{0};
    std::array<std::atomic<Sint16>, SDL_CONTROLLER_AXIS_MAX> controllerAxes{};
    std::mutex controllerNameMutex;
    std::string connectedControllerName;

    SDL_GameController* activeController = nullptr;
    SDL_JoystickID activeControllerInstance = -1;
    bool previousBackPressed = false;
    float cameraWheel = 0; // SDL/main thread only.
    bool appliedRumble = false;
    std::atomic_bool requestedRumble{false};
    std::atomic_bool backgroundGamepadInput{false};
    std::atomic<int> stickDeadZonePercent{
        doraemon::input::DefaultStickDeadZonePercent};

    std::mutex bindingsMutex;
    std::array<ActionBinding, ActionCount> bindings = defaultBindings();
    std::filesystem::path bindingsPath;

    std::atomic<int> captureAction{-1};
    std::atomic<int> captureDevice{static_cast<int>(BindingDevice::Keyboard)};
    std::atomic_bool captureWaitingForRelease{false};

    bool validAction(Action action) {
        return static_cast<std::size_t>(action) < ActionCount;
    }

    bool validGamepadInput(GamepadInput input) {
        return static_cast<std::size_t>(input) < GamepadInputCount;
    }

    bool pressed(const Uint8* keys, int keyCount, SDL_Scancode key) {
        return (key != SDL_SCANCODE_UNKNOWN) &&
            (static_cast<int>(key) >= 0) &&
            (static_cast<int>(key) < keyCount) &&
            (keys[key] != 0);
    }

    float positiveAxisValue(Sint16 value, Sint16 deadZone) {
        if (value <= deadZone) {
            return 0.0f;
        }
        return std::clamp(
            static_cast<float>(value - deadZone) / static_cast<float>(32767 - deadZone),
            0.0f, 1.0f);
    }

    float negativeAxisValue(Sint16 value, Sint16 deadZone) {
        if (value >= -deadZone) {
            return 0.0f;
        }
        return std::clamp(
            static_cast<float>(-static_cast<int>(value) - deadZone) /
                static_cast<float>(32768 - deadZone),
            0.0f, 1.0f);
    }

    Sint16 currentStickDeadZone() {
        const int percent = stickDeadZonePercent.load(std::memory_order_acquire);
        return static_cast<Sint16>(std::lround(
            static_cast<float>(SDL_JOYSTICK_AXIS_MAX) *
            static_cast<float>(percent) / 100.0f));
    }

    bool controllerButtonPressed(uint32_t value, SDL_GameControllerButton button) {
        return (value & (1u << static_cast<unsigned>(button))) != 0;
    }

    float gamepadValue(
        GamepadInput input,
        uint32_t buttonsValue,
        const std::array<Sint16, SDL_CONTROLLER_AXIS_MAX>& axes)
    {
        const Sint16 stickDeadZone = currentStickDeadZone();
        switch (input) {
        case GamepadInput::A: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_A) ? 1.0f : 0.0f;
        case GamepadInput::B: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_B) ? 1.0f : 0.0f;
        case GamepadInput::X: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_X) ? 1.0f : 0.0f;
        case GamepadInput::Y: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_Y) ? 1.0f : 0.0f;
        case GamepadInput::Start: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_START) ? 1.0f : 0.0f;
        case GamepadInput::LeftShoulder: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? 1.0f : 0.0f;
        case GamepadInput::RightShoulder: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 1.0f : 0.0f;
        case GamepadInput::LeftTrigger: return positiveAxisValue(axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT], TriggerDeadZone);
        case GamepadInput::RightTrigger: return positiveAxisValue(axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT], TriggerDeadZone);
        case GamepadInput::LeftStickButton: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_LEFTSTICK) ? 1.0f : 0.0f;
        case GamepadInput::RightStickButton: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_RIGHTSTICK) ? 1.0f : 0.0f;
        case GamepadInput::DpadUp: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_DPAD_UP) ? 1.0f : 0.0f;
        case GamepadInput::DpadDown: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? 1.0f : 0.0f;
        case GamepadInput::DpadLeft: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ? 1.0f : 0.0f;
        case GamepadInput::DpadRight: return controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? 1.0f : 0.0f;
        case GamepadInput::LeftStickUp: return negativeAxisValue(axes[SDL_CONTROLLER_AXIS_LEFTY], stickDeadZone);
        case GamepadInput::LeftStickDown: return positiveAxisValue(axes[SDL_CONTROLLER_AXIS_LEFTY], stickDeadZone);
        case GamepadInput::LeftStickLeft: return negativeAxisValue(axes[SDL_CONTROLLER_AXIS_LEFTX], stickDeadZone);
        case GamepadInput::LeftStickRight: return positiveAxisValue(axes[SDL_CONTROLLER_AXIS_LEFTX], stickDeadZone);
        case GamepadInput::RightStickUp: return negativeAxisValue(axes[SDL_CONTROLLER_AXIS_RIGHTY], stickDeadZone);
        case GamepadInput::RightStickDown: return positiveAxisValue(axes[SDL_CONTROLLER_AXIS_RIGHTY], stickDeadZone);
        case GamepadInput::RightStickLeft: return negativeAxisValue(axes[SDL_CONTROLLER_AXIS_RIGHTX], stickDeadZone);
        case GamepadInput::RightStickRight: return positiveAxisValue(axes[SDL_CONTROLLER_AXIS_RIGHTX], stickDeadZone);
        case GamepadInput::None:
        case GamepadInput::Count:
            return 0.0f;
        }
        return 0.0f;
    }

    std::array<Sint16, SDL_CONTROLLER_AXIS_MAX> loadControllerAxes() {
        std::array<Sint16, SDL_CONTROLLER_AXIS_MAX> axes{};
        for (int index = 0; index < SDL_CONTROLLER_AXIS_MAX; index++) {
            axes[index] = controllerAxes[index].load(std::memory_order_relaxed);
        }
        return axes;
    }

    void saveBindingsLocked() {
        if (bindingsPath.empty()) {
            return;
        }
        try {
            nlohmann::json json;
            json["version"] = 3;
            json["background_gamepad_input"] =
                backgroundGamepadInput.load(std::memory_order_acquire);
            json["stick_dead_zone_percent"] =
                stickDeadZonePercent.load(std::memory_order_acquire);
            for (std::size_t index = 0; index < ActionCount; index++) {
                json["bindings"][ActionKeys[index]]["keyboard"] = static_cast<int>(bindings[index].keyboard);
                json["bindings"][ActionKeys[index]]["gamepad"] = static_cast<int>(bindings[index].gamepad);
            }
            std::ofstream output(bindingsPath);
            if (output) {
                output << json.dump(4) << '\n';
            }
        }
        catch (const std::exception& exception) {
            std::fprintf(stderr, "Unable to save input settings: %s\n", exception.what());
        }
    }

    void loadBindings() {
        std::lock_guard lock(bindingsMutex);
        bindings = defaultBindings();
        backgroundGamepadInput.store(false, std::memory_order_release);
        stickDeadZonePercent.store(
            doraemon::input::DefaultStickDeadZonePercent,
            std::memory_order_release);
        if (bindingsPath.empty()) {
            return;
        }
        try {
            std::ifstream input(bindingsPath);
            if (!input) {
                return;
            }
            const nlohmann::json json = nlohmann::json::parse(input);
            backgroundGamepadInput.store(
                json.value("background_gamepad_input", false),
                std::memory_order_release);
            stickDeadZonePercent.store(
                std::clamp(
                    json.value(
                        "stick_dead_zone_percent",
                        doraemon::input::DefaultStickDeadZonePercent),
                    doraemon::input::MinimumStickDeadZonePercent,
                    doraemon::input::MaximumStickDeadZonePercent),
                std::memory_order_release);
            if (!json.contains("bindings")) {
                return;
            }
            const auto& jsonBindings = json.at("bindings");
            for (std::size_t index = 0; index < ActionCount; index++) {
                if (!jsonBindings.contains(ActionKeys[index])) {
                    continue;
                }
                const auto& entry = jsonBindings.at(ActionKeys[index]);
                const int keyboard = entry.value("keyboard", static_cast<int>(bindings[index].keyboard));
                const int gamepad = entry.value("gamepad", static_cast<int>(bindings[index].gamepad));
                if ((keyboard >= SDL_SCANCODE_UNKNOWN) && (keyboard < SDL_NUM_SCANCODES)) {
                    bindings[index].keyboard = static_cast<SDL_Scancode>(keyboard);
                }
                if ((gamepad >= 0) && (gamepad < static_cast<int>(GamepadInput::Count))) {
                    bindings[index].gamepad = static_cast<GamepadInput>(gamepad);
                }
            }
        }
        catch (const std::exception& exception) {
            std::fprintf(stderr, "Unable to load input settings: %s\n", exception.what());
        }
    }

    void clearControllerState() {
        controllerButtons.store(0, std::memory_order_release);
        for (auto& axis : controllerAxes) {
            axis.store(0, std::memory_order_release);
        }
        previousBackPressed = false;
    }

    void closeController() {
        if (activeController != nullptr) {
            SDL_GameControllerRumble(activeController, 0, 0, 0);
            SDL_GameControllerClose(activeController);
            activeController = nullptr;
        }
        activeControllerInstance = -1;
        controllerConnected.store(false, std::memory_order_release);
        clearControllerState();
        appliedRumble = false;
        std::lock_guard lock(controllerNameMutex);
        connectedControllerName.clear();
    }

    bool openController(int deviceIndex) {
        if ((activeController != nullptr) || !SDL_IsGameController(deviceIndex)) {
            return false;
        }
        SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex);
        if (controller == nullptr) {
            std::fprintf(stderr, "Unable to open gamepad %d: %s\n", deviceIndex, SDL_GetError());
            return false;
        }
        activeController = controller;
        activeControllerInstance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
        controllerConnected.store(true, std::memory_order_release);
        clearControllerState();
        const char* name = SDL_GameControllerName(controller);
        {
            std::lock_guard lock(controllerNameMutex);
            connectedControllerName = (name != nullptr) ? name : "SDL gamepad";
        }
        std::printf("Gamepad connected: %s\n", (name != nullptr) ? name : "SDL gamepad");
        return true;
    }

    void scanForController() {
        if (activeController != nullptr) {
            return;
        }
        for (int index = 0; index < SDL_NumJoysticks(); index++) {
            if (openController(index)) {
                return;
            }
        }
    }

    bool anyKeyboardInput(const Uint8* keys, int keyCount) {
        for (int index = 1;
             index < std::min(keyCount, static_cast<int>(SDL_NUM_SCANCODES));
             index++) {
            if (keys[index] != 0) {
                return true;
            }
        }
        return false;
    }

    SDL_Scancode firstKeyboardInput(const Uint8* keys, int keyCount) {
        for (int index = 1;
             index < std::min(keyCount, static_cast<int>(SDL_NUM_SCANCODES));
             index++) {
            if (keys[index] != 0) {
                return static_cast<SDL_Scancode>(index);
            }
        }
        return SDL_SCANCODE_UNKNOWN;
    }

    GamepadInput firstGamepadInput(
        uint32_t buttonsValue,
        const std::array<Sint16, SDL_CONTROLLER_AXIS_MAX>& axes)
    {
        for (std::size_t index = 1; index < GamepadInputCount; index++) {
            const auto input = static_cast<GamepadInput>(index);
            if (gamepadValue(input, buttonsValue, axes) >= 0.6f) {
                return input;
            }
        }
        return GamepadInput::None;
    }

    void handleBindingCapture(
        const Uint8* keys,
        int keyCount,
        uint32_t buttonsValue,
        const std::array<Sint16, SDL_CONTROLLER_AXIS_MAX>& axes)
    {
        const int actionValue = captureAction.load(std::memory_order_acquire);
        if ((actionValue < 0) || (actionValue >= static_cast<int>(Action::Count))) {
            return;
        }
        const auto device = static_cast<BindingDevice>(captureDevice.load(std::memory_order_acquire));
        const bool hasInput = (device == BindingDevice::Keyboard)
            ? anyKeyboardInput(keys, keyCount)
            : (firstGamepadInput(buttonsValue, axes) != GamepadInput::None);
        if (captureWaitingForRelease.load(std::memory_order_acquire)) {
            if (!hasInput) {
                captureWaitingForRelease.store(false, std::memory_order_release);
            }
            return;
        }

        const Action action = static_cast<Action>(actionValue);
        if (device == BindingDevice::Keyboard) {
            const SDL_Scancode scancode = firstKeyboardInput(keys, keyCount);
            if (scancode == SDL_SCANCODE_UNKNOWN) {
                return;
            }
            if (scancode == SDL_SCANCODE_ESCAPE) {
                return;
            }
            std::lock_guard lock(bindingsMutex);
            bindings[static_cast<std::size_t>(action)].keyboard = scancode;
            saveBindingsLocked();
        }
        else {
            const GamepadInput input = firstGamepadInput(buttonsValue, axes);
            if (input == GamepadInput::None) {
                return;
            }
            std::lock_guard lock(bindingsMutex);
            bindings[static_cast<std::size_t>(action)].gamepad = input;
            saveBindingsLocked();
        }
        captureAction.store(-1, std::memory_order_release);
    }
}

void doraemon::input::initialize(const std::filesystem::path& configDirectory) {
    doraemon::touch::initialize(configDirectory);
    bindingsPath = configDirectory / "doraemon_input_settings.json";
    std::error_code settingsError;
    const bool firstRun = !std::filesystem::exists(bindingsPath, settingsError) && !settingsError;
    loadBindings();
    if (firstRun) {
        set_background_gamepad_input_enabled(true);
    }
    else {
        SDL_SetHint(
            SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
            is_background_gamepad_input_enabled() ? "1" : "0");
    }
}

void doraemon::input::initialize_sdl() {
    scanForController();
}

void doraemon::input::shutdown() {
    doraemon::touch::cancel();
    cancel_binding_capture();
    SDL_SetRelativeMouseMode(SDL_FALSE);
    closeController();
}

void doraemon::input::process_event(const SDL_Event& event) {
    // Only the selected controller can switch the touch HUD to gamepad mode.
    const bool controllerEvent = event.type == SDL_CONTROLLERBUTTONDOWN ||
        event.type == SDL_CONTROLLERBUTTONUP || event.type == SDL_CONTROLLERAXISMOTION ||
        event.type == SDL_CONTROLLERDEVICEREMOVED;
    if (!controllerEvent || event.cdevice.which == activeControllerInstance)
        doraemon::touch::process_event(event);
    if (event.type == SDL_MOUSEWHEEL && doraemon::camera::is_active() &&
        SDL_GetKeyboardFocus() != nullptr && !doraemon::system_overlay::is_menu_open() &&
        !is_binding_capture_active()) {
        cameraWheel += float(event.wheel.y) * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
    }
    if (event.type == SDL_CONTROLLERDEVICEADDED) {
        if (activeController == nullptr) {
            openController(event.cdevice.which);
        }
    }
    else if ((event.type == SDL_CONTROLLERDEVICEREMOVED) &&
             (event.cdevice.which == activeControllerInstance)) {
        std::puts("Gamepad disconnected.");
        closeController();
        scanForController();
    }
}

void doraemon::input::update() {
    doraemon::touch::update();
    int keyCount = 0;
    const Uint8* keys = SDL_GetKeyboardState(&keyCount);
    const bool windowFocused = SDL_GetKeyboardFocus() != nullptr;
    const int activeKeyCount = ((keys != nullptr) && windowFocused) ? keyCount : 0;
    const bool acceptGamepadInput =
        windowFocused || backgroundGamepadInput.load(std::memory_order_acquire);

    uint32_t buttonsValue = 0;
    std::array<Sint16, SDL_CONTROLLER_AXIS_MAX> axes{};
    if (activeController != nullptr) {
        if (acceptGamepadInput) {
            for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
                if (SDL_GameControllerGetButton(activeController, static_cast<SDL_GameControllerButton>(button)) != 0) {
                    buttonsValue |= 1u << static_cast<unsigned>(button);
                }
            }
            for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; axis++) {
                axes[axis] = SDL_GameControllerGetAxis(activeController, static_cast<SDL_GameControllerAxis>(axis));
            }
        }

        const bool rumble = requestedRumble.load(std::memory_order_acquire);
        if (rumble != appliedRumble) {
            SDL_GameControllerRumble(
                activeController,
                rumble ? 0xFFFF : 0,
                rumble ? 0xFFFF : 0,
                rumble ? 1000 : 0);
            appliedRumble = rumble;
        }
    }

    controllerButtons.store(buttonsValue, std::memory_order_release);
    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; axis++) {
        controllerAxes[axis].store(axes[axis], std::memory_order_release);
    }

    const bool backPressed = controllerButtonPressed(buttonsValue, SDL_CONTROLLER_BUTTON_BACK);
    if (backPressed && !previousBackPressed) {
        if (is_binding_capture_active()) {
            cancel_binding_capture();
        }
        else {
            doraemon::system_overlay::toggle_menu();
        }
    }
    previousBackPressed = backPressed;

    handleBindingCapture(keys, activeKeyCount, buttonsValue, axes);

    const bool modernCamera = doraemon::camera::is_active() &&
        !doraemon::system_overlay::is_menu_open() && !is_binding_capture_active();
#if defined(__ANDROID__)
    const bool captureMouse = false; // Touch-generated mouse events belong to the port menu.
#else
    const bool captureMouse = modernCamera && windowFocused && doraemon::camera::is_mouse_capture_enabled();
#endif
    const bool wasCaptured = SDL_GetRelativeMouseMode() == SDL_TRUE;
    if (captureMouse != wasCaptured) {
        SDL_SetRelativeMouseMode(captureMouse ? SDL_TRUE : SDL_FALSE);
    }
    const float wheel = cameraWheel;
    cameraWheel = 0;
    int mouseX = 0, mouseY = 0;
    SDL_GetRelativeMouseState(&mouseX, &mouseY);
    if (!captureMouse || !wasCaptured) mouseX = mouseY = 0;


    if (doraemon::system_overlay::is_menu_open() || doraemon::touch::is_editing()) {
        doraemon::camera::clear_input();
        currentButtons.store(0, std::memory_order_release);
        currentStickX.store(0.0f, std::memory_order_release);
        currentStickY.store(0.0f, std::memory_order_release);
        return;
    }

    std::array<ActionBinding, ActionCount> currentBindings{};
    {
        std::lock_guard lock(bindingsMutex);
        currentBindings = bindings;
    }
    const auto value = [&](Action action) {
        const ActionBinding& binding = currentBindings[static_cast<std::size_t>(action)];
        const float keyboardValue = pressed(keys, activeKeyCount, binding.keyboard) ? 1.0f : 0.0f;
        return std::max(keyboardValue, gamepadValue(binding.gamepad, buttonsValue, axes));
    };
    const auto active = [&](Action action) { return value(action) >= 0.5f; };

    const auto touchCamera = doraemon::touch::take_camera();
    if (modernCamera) {
        const float vertical = value(Action::CameraDown) - value(Action::CameraUp);
        const bool zoomModifier = active(Action::CameraMode);
        // Use logical D-pad actions so both keyboard and gamepad remapping apply.
        const float zoom = std::max(value(Action::CameraZoomOut), value(Action::DpadDown)) -
            std::max(value(Action::CameraZoomIn), value(Action::DpadUp)) +
            (zoomModifier ? vertical : 0.0f);
        doraemon::camera::submit_input(value(Action::CameraRight) - value(Action::CameraLeft),
            zoomModifier ? 0.0f : vertical, float(mouseX) + touchCamera.dx, float(mouseY) + touchCamera.dy, zoom + touchCamera.zoom,
            windowFocused ? wheel : 0.0f);
    }
    else {
        doraemon::camera::clear_input();
    }

    uint16_t buttons = 0;
    if (active(Action::JumpConfirm)) buttons |= N64_A;
    if (active(Action::AttackBack)) buttons |= N64_B;
    if (active(Action::Crouch)) buttons |= N64_Z;
    if (active(Action::Pause)) buttons |= N64_START;
    if (active(Action::LeftTrigger)) buttons |= N64_L;
    if (!modernCamera && active(Action::CameraMode)) buttons |= N64_R;
    if (!modernCamera && active(Action::DpadUp)) buttons |= N64_DPAD_UP;
    if (!modernCamera && active(Action::DpadDown)) buttons |= N64_DPAD_DOWN;
    if (active(Action::DpadLeft)) buttons |= N64_DPAD_LEFT;
    if (active(Action::DpadRight)) buttons |= N64_DPAD_RIGHT;
    if (!modernCamera && active(Action::CameraUp)) buttons |= N64_C_UP;
    if (!modernCamera && active(Action::CameraDown)) buttons |= N64_C_DOWN;
    if (!modernCamera && active(Action::CameraLeft)) buttons |= N64_C_LEFT;
    if (!modernCamera && active(Action::CameraRight)) buttons |= N64_C_RIGHT;

    float x = value(Action::MoveRight) - value(Action::MoveLeft);
    float y = value(Action::MoveUp) - value(Action::MoveDown);
    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude > 1.0f) {
        x /= magnitude;
        y /= magnitude;
    }

    currentButtons.store(buttons, std::memory_order_release);
    currentStickX.store(x, std::memory_order_release);
    currentStickY.store(y, std::memory_order_release);
}

void doraemon::input::poll() {
    // SDL is pumped and sampled on the main thread by update_gfx.
}

bool doraemon::input::get_input(int controllerNum, uint16_t* buttons, float* x, float* y) {
    if (controllerNum != 0) {
        return false;
    }
    if (doraemon::system_overlay::is_menu_open() || doraemon::touch::is_editing()) {
        *buttons=0; *x=0; *y=0;
        return true;
    }
    const auto touch = doraemon::touch::read_input();
    *buttons = currentButtons.load(std::memory_order_acquire) | touch.buttons;
    *x = currentStickX.load(std::memory_order_acquire);
    *y = currentStickY.load(std::memory_order_acquire);
    if (touch.stickOwned) { *x=touch.x; *y=touch.y; }
    return true;
}

void doraemon::input::set_rumble(int controllerNum, bool rumble) {
    if (controllerNum == 0) {
        requestedRumble.store(rumble, std::memory_order_release);
    }
}

ultramodern::input::connected_device_info_t doraemon::input::get_connected_device_info(int controllerNum) {
    if (controllerNum == 0) {
        return {
            ultramodern::input::Device::Controller,
            is_gamepad_connected() ? ultramodern::input::Pak::RumblePak : ultramodern::input::Pak::None
        };
    }
    return { ultramodern::input::Device::None, ultramodern::input::Pak::None };
}

const char* doraemon::input::action_name(Action action) {
    return validAction(action) ? ActionNames[static_cast<std::size_t>(action)] : "Unknown action";
}

std::string doraemon::input::keyboard_binding_name(Action action) {
    const ActionBinding binding = get_binding(action);
    if (binding.keyboard == SDL_SCANCODE_UNKNOWN) {
        return "Unbound";
    }
    const char* name = SDL_GetScancodeName(binding.keyboard);
    return ((name != nullptr) && (name[0] != '\0')) ? name : "Unknown key";
}

std::string doraemon::input::gamepad_binding_name(Action action) {
    const ActionBinding binding = get_binding(action);
    return validGamepadInput(binding.gamepad)
        ? GamepadInputNames[static_cast<std::size_t>(binding.gamepad)]
        : "Unbound";
}

ActionBinding doraemon::input::get_binding(Action action) {
    if (!validAction(action)) {
        return {};
    }
    std::lock_guard lock(bindingsMutex);
    return bindings[static_cast<std::size_t>(action)];
}

void doraemon::input::set_keyboard_binding(Action action, SDL_Scancode scancode) {
    if (!validAction(action) || (scancode < SDL_SCANCODE_UNKNOWN) || (scancode >= SDL_NUM_SCANCODES)) {
        return;
    }
    std::lock_guard lock(bindingsMutex);
    bindings[static_cast<std::size_t>(action)].keyboard = scancode;
    saveBindingsLocked();
}

void doraemon::input::set_gamepad_binding(Action action, GamepadInput input) {
    if (!validAction(action) || !validGamepadInput(input)) {
        return;
    }
    std::lock_guard lock(bindingsMutex);
    bindings[static_cast<std::size_t>(action)].gamepad = input;
    saveBindingsLocked();
}

void doraemon::input::reset_bindings() {
    std::lock_guard lock(bindingsMutex);
    bindings = defaultBindings();
    saveBindingsLocked();
}

void doraemon::input::begin_binding_capture(Action action, BindingDevice device) {
    if (!validAction(action)) {
        return;
    }
    captureDevice.store(static_cast<int>(device), std::memory_order_release);
    captureWaitingForRelease.store(true, std::memory_order_release);
    captureAction.store(static_cast<int>(action), std::memory_order_release);
}

void doraemon::input::cancel_binding_capture() {
    captureAction.store(-1, std::memory_order_release);
    captureWaitingForRelease.store(false, std::memory_order_release);
}

bool doraemon::input::is_binding_capture_active() {
    return captureAction.load(std::memory_order_acquire) >= 0;
}

bool doraemon::input::is_gamepad_connected() {
    return controllerConnected.load(std::memory_order_acquire);
}

std::string doraemon::input::gamepad_name() {
    std::lock_guard lock(controllerNameMutex);
    return connectedControllerName;
}

float doraemon::input::gamepad_input_value(GamepadInput input) {
    return gamepadValue(
        input,
        controllerButtons.load(std::memory_order_acquire),
        loadControllerAxes());
}

int doraemon::input::stick_dead_zone_percent() {
    return stickDeadZonePercent.load(std::memory_order_acquire);
}

void doraemon::input::set_stick_dead_zone_percent(int percent) {
    const int clampedPercent = std::clamp(
        percent,
        MinimumStickDeadZonePercent,
        MaximumStickDeadZonePercent);
    if (stickDeadZonePercent.exchange(
            clampedPercent,
            std::memory_order_acq_rel) == clampedPercent) {
        return;
    }

    std::lock_guard lock(bindingsMutex);
    saveBindingsLocked();
}

bool doraemon::input::is_background_gamepad_input_enabled() {
    return backgroundGamepadInput.load(std::memory_order_acquire);
}

void doraemon::input::set_background_gamepad_input_enabled(bool enabled) {
    backgroundGamepadInput.store(enabled, std::memory_order_release);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, enabled ? "1" : "0");

    std::lock_guard lock(bindingsMutex);
    saveBindingsLocked();
}
