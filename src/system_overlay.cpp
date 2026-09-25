#include "doraemon_draw_distance.h"
#include "doraemon_camera.hpp"
#include "doraemon_cheats.h"
#include "system_overlay.hpp"
#include "doraemon_touch.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "doraemon_audio.hpp"
#include "doraemon_input.hpp"
#include "doraemon_localization.hpp"
#include "imgui/imgui.h"
#include "json/json.hpp"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"
#include <SDL.h>

namespace {
    class FpsCounter {
    public:
        float nextFrame(std::uint64_t frames) {
            using Clock = std::chrono::steady_clock;

            const auto now = Clock::now();
            frameCount += frames;
            const std::chrono::duration<float> elapsed = now - sampleStart;
            if (elapsed.count() >= samplePeriodSeconds) {
                const float current = static_cast<float>(frameCount) / elapsed.count();
                framesPerSecond = hasSample
                    ? (framesPerSecond * 0.25f + current * 0.75f)
                    : current;
                hasSample = true;
                frameCount = 0;
                sampleStart = now;
            }

            return framesPerSecond;
        }

        bool ready() const {
            return hasSample;
        }

    private:
        static constexpr float samplePeriodSeconds = 0.5f;
        std::chrono::steady_clock::time_point sampleStart =
            std::chrono::steady_clock::now();
        std::uint64_t frameCount = 0;
        float framesPerSecond = 0.0f;
        bool hasSample = false;
    };

    struct PcSettings {
        bool autosave = true;
        float drawDistance = 1.0f;
        float modelLodDistance = 5.0f;
        bool cheatHealth = false;
        bool cheatLives = false;
        bool cheatTorpedo = false;
        bool modernCamera = false;
        bool cameraCaptureMouse = false;
        bool cameraInvertX = false;
        bool cameraInvertY = false;
        float cameraStickSpeed = 120.0f;
        float cameraMouseSensitivity = 0.15f;
        bool showFps = true;
        float masterVolume = 1.0f;
        std::string languageCode = "ja";
        ultramodern::renderer::GraphicsConfig graphics{};
    };

    struct OutputMode {
        int width = 0;
        int height = 0;
        int refreshRate = 0;

        bool operator==(const OutputMode&) const = default;
    };

    struct DisplayInfo {
        std::string name;
        OutputMode desktopMode;
        std::vector<OutputMode> modes;
    };

    // Dimensions below are authored at a 720p output height. Use ImGui's
    // viewport coordinates (already accounting for framebuffer DPI), not the
    // game's internal render resolution.
    float overlayScale = 1.0f;

    float uiPixels(float pixels) {
        return pixels * overlayScale;
    }

    class ScopedOverlayScale {
    public:
        ScopedOverlayScale()
            : originalStyle(ImGui::GetStyle()),
              originalFontScale(ImGui::GetIO().FontGlobalScale) {
            const float height = ImGui::GetMainViewport()->WorkSize.y;
            overlayScale = height > 0.0f ? height / 720.0f : 1.0f;
            // Start from the unscaled style every frame: resizing must not
            // accumulate scaling or rounding, or affect the RT64 inspector.
            ImGui::GetStyle().ScaleAllSizes(overlayScale);
            ImGui::GetIO().FontGlobalScale = originalFontScale * overlayScale;
            ImGui::PushFont(ImGui::GetFont());
        }

        ~ScopedOverlayScale() {
            ImGui::GetIO().FontGlobalScale = originalFontScale;
            ImGui::PopFont();
            ImGui::GetStyle() = originalStyle;
            overlayScale = 1.0f;
        }

        ScopedOverlayScale(const ScopedOverlayScale&) = delete;
        ScopedOverlayScale& operator=(const ScopedOverlayScale&) = delete;

    private:
        ImGuiStyle originalStyle;
        float originalFontScale;
    };

    FpsCounter fpsCounter;
    std::atomic<std::uint64_t> outputFrames{0};
    PcSettings settings;
    std::filesystem::path settingsPath;
    std::atomic_bool autosaveEnabled = true;
    std::atomic<std::int64_t> autosaveNotificationUntilNs = 0;
    std::atomic_bool menuOpen = false;
    std::atomic_bool gameResetRequested = false;
    std::vector<DisplayInfo> displays;
    doraemon::input::Action captureAction = doraemon::input::Action::Count;
    doraemon::input::BindingDevice captureDevice =
        doraemon::input::BindingDevice::Keyboard;

    bool menuSelectable(const char* label, bool selected) {
#if defined(__ANDROID__)
        // Combo rows need their own height; FramePadding only grows the closed combo.
        return ImGui::Selectable(label, selected, 0,
            ImVec2(0, ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.y));
#else
        return ImGui::Selectable(label, selected);
#endif
    }

    template <typename Enum>
    bool validEnum(Enum value) {
        return static_cast<int>(value) >= 0 &&
            static_cast<int>(value) < static_cast<int>(Enum::OptionCount);
    }

    void saveSettings() {
        if (settingsPath.empty()) {
            return;
        }

        try {
            nlohmann::json json;
            json["version"] = 14;
            json["game"]["autosave"] = settings.autosave;
            json["cheats"]["infinite_health"] = settings.cheatHealth;
            json["cheats"]["infinite_lives"] = settings.cheatLives;
            json["cheats"]["first_race_torpedo"] = settings.cheatTorpedo;
            json["camera"]["modern"] = settings.modernCamera;
            json["camera"]["capture_mouse"] = settings.cameraCaptureMouse;
            json["camera"]["invert_x"] = settings.cameraInvertX;
            json["camera"]["invert_y"] = settings.cameraInvertY;
            json["camera"]["stick_speed"] = settings.cameraStickSpeed;
            json["camera"]["mouse_sensitivity"] = settings.cameraMouseSensitivity;
            json["show_fps"] = settings.showFps;
            json["master_volume"] = settings.masterVolume;
            json["language"]["code"] = settings.languageCode;
            json["graphics"]["frame_rate_mode"] = settings.graphics.rr_option;
            json["graphics"]["frame_rate_limit"] = settings.graphics.rr_manual_value;
            json["graphics"]["draw_distance"] = settings.drawDistance;
            json["graphics"]["model_lod_distance"] = settings.modelLodDistance;
            json["graphics"]["window_mode"] = settings.graphics.wm_option;
            json["graphics"]["display_index"] = settings.graphics.display_index;
            json["graphics"]["display_width"] = settings.graphics.display_width;
            json["graphics"]["display_height"] = settings.graphics.display_height;
            json["graphics"]["display_refresh_rate"] = settings.graphics.display_refresh_rate;
            json["graphics"]["render_resolution"] = settings.graphics.res_option;
            json["graphics"]["aspect_ratio"] = settings.graphics.ar_option;
            json["graphics"]["hud_layout"] = settings.graphics.hr_option;
            json["graphics"]["antialiasing"] = settings.graphics.msaa_option;
            json["graphics"]["texture_filtering"] = settings.graphics.tf_option;

            std::ofstream output(settingsPath);
            if (output) {
                output << json.dump(4) << '\n';
            }
        }
        catch (const std::exception& exception) {
            std::fprintf(stderr, "Unable to save PC settings: %s\n", exception.what());
        }
    }

    std::string systemLanguageCode() {
        const auto& languages = doraemon::localization::available_languages();
        std::string code = languages.front().code;
        SDL_Locale* locales = SDL_GetPreferredLocales();
        if (locales != nullptr) {
            for (const SDL_Locale* locale = locales;
                 locale->language != nullptr; ++locale) {
                const auto match = std::find_if(
                    languages.begin(), languages.end(),
                    [locale](const auto& language) {
                        return SDL_strcasecmp(
                            language.code.c_str(), locale->language) == 0;
                    });
                if (match != languages.end()) {
                    code = match->code;
                    break;
                }
            }
            SDL_free(locales);
        }
        return code;
    }

    void loadSettings() {
        if (settingsPath.empty()) {
            return;
        }

        try {
            std::ifstream input(settingsPath);
            if (!input) {
                return;
            }

            const nlohmann::json json = nlohmann::json::parse(input);
            settings.showFps = json.value("show_fps", settings.showFps);
            settings.masterVolume = std::clamp(
                json.value("master_volume", settings.masterVolume), 0.0f, 1.0f);

            if (json.contains("game")) {
                const auto& game = json.at("game");
                settings.autosave = game.value("autosave", settings.autosave);
            }

            if (json.contains("cheats")) {
                const auto& cheats = json.at("cheats");
                settings.cheatHealth = cheats.value("infinite_health", false);
                settings.cheatLives = cheats.value("infinite_lives", false);
                settings.cheatTorpedo = cheats.value("first_race_torpedo", false);
            }

            if (json.contains("camera")) {
                const auto& camera = json.at("camera");
                settings.modernCamera = camera.value("modern", false);
                settings.cameraCaptureMouse = camera.value("capture_mouse", false);
                settings.cameraInvertX = camera.value("invert_x", false);
                settings.cameraInvertY = camera.value("invert_y", false);
                settings.cameraStickSpeed = std::clamp(camera.value("stick_speed", 120.0f), 30.0f, 360.0f);
                settings.cameraMouseSensitivity = std::clamp(camera.value("mouse_sensitivity", 0.15f), 0.02f, 1.0f);
            }

            if (json.contains("language")) {
                const auto& language = json.at("language");
                if (language.contains("code") && language.at("code").is_string()) {
                    settings.languageCode = language.at("code").get<std::string>();
                }
                else if (language.contains("dialogue") &&
                         language.at("dialogue").is_number_integer()) {
                    // Settings versions up to 6 stored the hard-coded language enum.
                    switch (language.at("dialogue").get<int>()) {
                        case 1:
                            settings.languageCode = "en";
                            break;
                        case 2:
                            settings.languageCode = "ru";
                            break;
                        default:
                            settings.languageCode = "ja";
                            break;
                    }
                }
            }

            if (json.contains("graphics")) {
                const auto& graphics = json.at("graphics");
                settings.drawDistance = graphics.value("draw_distance", 1.0f);
                settings.modelLodDistance = graphics.value("model_lod_distance", 5.0f);
                const auto frameRate = graphics.value(
                    "frame_rate_mode", ultramodern::renderer::RefreshRate::Original);
                if (validEnum(frameRate)) {
                    settings.graphics.rr_option = frameRate;
                }
                settings.graphics.rr_manual_value = std::clamp(
                    graphics.value("frame_rate_limit", 144), 60, 360);
                const auto windowMode = graphics.value(
                    "window_mode", settings.graphics.wm_option);
                const auto resolution = graphics.value(
                    "render_resolution", settings.graphics.res_option);
                const auto aspectRatio = graphics.value(
                    "aspect_ratio", settings.graphics.ar_option);
                const auto hudLayout = graphics.value(
                    "hud_layout", settings.graphics.hr_option);
                const auto antialiasing = graphics.value(
                    "antialiasing", settings.graphics.msaa_option);
                const auto textureFiltering = graphics.value(
                    "texture_filtering", settings.graphics.tf_option);

                if (validEnum(windowMode)) {
                    settings.graphics.wm_option = windowMode;
                }
                settings.graphics.display_index = std::max(
                    graphics.value("display_index", settings.graphics.display_index), 0);
                settings.graphics.display_width = std::clamp(
                    graphics.value("display_width", settings.graphics.display_width), 320, 16384);
                settings.graphics.display_height = std::clamp(
                    graphics.value("display_height", settings.graphics.display_height), 240, 16384);
                settings.graphics.display_refresh_rate = std::clamp(
                    graphics.value("display_refresh_rate", settings.graphics.display_refresh_rate), 0, 1000);
                if (validEnum(resolution)) {
                    settings.graphics.res_option = resolution;
                }
                if (validEnum(aspectRatio)) {
                    settings.graphics.ar_option = aspectRatio;
                }
                if ((hudLayout == ultramodern::renderer::HUDRatioMode::Original) ||
                    (hudLayout == ultramodern::renderer::HUDRatioMode::Full)) {
                    settings.graphics.hr_option = hudLayout;
                }
                if (validEnum(antialiasing)) {
                    settings.graphics.msaa_option = antialiasing;
                }
                if (validEnum(textureFiltering)) {
                    settings.graphics.tf_option = textureFiltering;
                }
            }
        }
        catch (const std::exception& exception) {
            std::fprintf(stderr, "Unable to load PC settings: %s\n", exception.what());
        }
    }

    void applyGraphicsSettings() {
        ultramodern::renderer::set_graphics_config(settings.graphics);
        saveSettings();
    }

    void refreshDisplays() {
        displays.clear();

        const int displayCount = SDL_GetNumVideoDisplays();
        if (displayCount <= 0) {
            std::fprintf(stderr, "Unable to enumerate displays: %s\n", SDL_GetError());
            return;
        }

        for (int displayIndex = 0; displayIndex < displayCount; displayIndex++) {
            DisplayInfo display;
            const char* displayName = SDL_GetDisplayName(displayIndex);
            display.name = std::to_string(displayIndex + 1) + ": " +
                ((displayName != nullptr) ? displayName : "Display");

            SDL_DisplayMode desktopMode{};
            if (SDL_GetDesktopDisplayMode(displayIndex, &desktopMode) == 0) {
                display.desktopMode = {
                    desktopMode.w,
                    desktopMode.h,
                    desktopMode.refresh_rate
                };
            }

            const int modeCount = SDL_GetNumDisplayModes(displayIndex);
            for (int modeIndex = 0; modeIndex < modeCount; modeIndex++) {
                SDL_DisplayMode mode{};
                if ((SDL_GetDisplayMode(displayIndex, modeIndex, &mode) == 0) &&
                    (mode.w >= 320) && (mode.h >= 240)) {
                    display.modes.push_back({ mode.w, mode.h, mode.refresh_rate });
                }
            }

            if ((display.desktopMode.width > 0) &&
                (std::find(display.modes.begin(), display.modes.end(), display.desktopMode) == display.modes.end())) {
                display.modes.push_back(display.desktopMode);
            }

            std::sort(display.modes.begin(), display.modes.end(), [](const OutputMode& left, const OutputMode& right) {
                if (left.width != right.width) {
                    return left.width < right.width;
                }
                if (left.height != right.height) {
                    return left.height < right.height;
                }
                return left.refreshRate < right.refreshRate;
            });
            display.modes.erase(
                std::unique(display.modes.begin(), display.modes.end()),
                display.modes.end());

            displays.push_back(std::move(display));
        }
    }

    const DisplayInfo* selectedDisplay() {
        if (displays.empty()) {
            return nullptr;
        }

        settings.graphics.display_index = std::clamp(
            settings.graphics.display_index, 0, static_cast<int>(displays.size()) - 1);
        return &displays[settings.graphics.display_index];
    }

    std::string outputModeLabel(int width, int height, int refreshRate, bool includeRefreshRate) {
        std::string label = std::to_string(width) + " x " + std::to_string(height);
        if (includeRefreshRate && (refreshRate > 0)) {
            label += " @ " + std::to_string(refreshRate) + " Hz";
        }
        return label;
    }

    bool displayCombo() {
        const DisplayInfo* currentDisplay = selectedDisplay();
        if (currentDisplay == nullptr) {
            ImGui::TextDisabled("No displays were reported by SDL.");
            return false;
        }

        bool changed = false;
        if (ImGui::BeginCombo("Monitor", currentDisplay->name.c_str())) {
            for (int displayIndex = 0; displayIndex < static_cast<int>(displays.size()); displayIndex++) {
                const bool isSelected = settings.graphics.display_index == displayIndex;
                if (menuSelectable(displays[displayIndex].name.c_str(), isSelected)) {
                    settings.graphics.display_index = displayIndex;
                    const OutputMode& desktopMode = displays[displayIndex].desktopMode;
                    if ((desktopMode.width > 0) && (desktopMode.height > 0)) {
                        settings.graphics.display_width = desktopMode.width;
                        settings.graphics.display_height = desktopMode.height;
                        settings.graphics.display_refresh_rate = desktopMode.refreshRate;
                    }
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool outputModeCombo(const DisplayInfo& display, bool includeRefreshRate) {
        const std::string preview = outputModeLabel(
            settings.graphics.display_width,
            settings.graphics.display_height,
            settings.graphics.display_refresh_rate,
            includeRefreshRate);

        std::vector<OutputMode> selectableModes;
        if (includeRefreshRate) {
            selectableModes = display.modes;
        }
        else {
            constexpr OutputMode commonWindowSizes[] = {
                { 640, 480, 0 },
                { 800, 600, 0 },
                { 960, 540, 0 },
                { 1024, 768, 0 },
                { 1280, 720, 0 },
                { 1600, 900, 0 },
                { 1920, 1080, 0 },
                { 2560, 1440, 0 },
                { 3840, 2160, 0 }
            };
            selectableModes.assign(
                std::begin(commonWindowSizes), std::end(commonWindowSizes));
            for (const OutputMode& mode : display.modes) {
                selectableModes.push_back({ mode.width, mode.height, 0 });
            }
            std::sort(selectableModes.begin(), selectableModes.end(), [](const OutputMode& left, const OutputMode& right) {
                if (left.width != right.width) {
                    return left.width < right.width;
                }
                return left.height < right.height;
            });
            selectableModes.erase(
                std::unique(selectableModes.begin(), selectableModes.end()),
                selectableModes.end());
        }

        bool changed = false;
        if (ImGui::BeginCombo("Output resolution", preview.c_str())) {
            for (const OutputMode& mode : selectableModes) {
                const bool isSelected =
                    (settings.graphics.display_width == mode.width) &&
                    (settings.graphics.display_height == mode.height) &&
                    (!includeRefreshRate ||
                        (settings.graphics.display_refresh_rate == mode.refreshRate));
                const std::string label = outputModeLabel(
                    mode.width, mode.height, mode.refreshRate, includeRefreshRate);
                if (menuSelectable(label.c_str(), isSelected)) {
                    settings.graphics.display_width = mode.width;
                    settings.graphics.display_height = mode.height;
                    settings.graphics.display_refresh_rate =
                        includeRefreshRate ? mode.refreshRate : 0;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void drawFps(float fps) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 position(
            viewport->WorkPos.x + viewport->WorkSize.x - uiPixels(12.0f),
            viewport->WorkPos.y + uiPixels(12.0f));

        ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.72f);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, uiPixels(5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(uiPixels(8.0f), uiPixels(5.0f)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.025f, 0.03f, 0.04f, 1.0f));
        if (ImGui::Begin("##DoraemonSystemOverlayFps", nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 1.0f, 0.76f, 1.0f));
            if (fpsCounter.ready()) {
                ImGui::Text("FPS %.1f", fps);
            }
            else {
                ImGui::TextUnformatted("FPS --");
            }
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void drawCheatsIndicator() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 position(
            viewport->WorkPos.x + viewport->WorkSize.x - uiPixels(12.0f),
            viewport->WorkPos.y + viewport->WorkSize.y - uiPixels(12.0f));

        ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.72f);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, uiPixels(5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(uiPixels(8.0f), uiPixels(5.0f)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.025f, 0.03f, 0.04f, 1.0f));
        if (ImGui::Begin("##DoraemonSystemOverlayCheats", nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 1.0f, 0.76f, 1.0f));
            ImGui::TextUnformatted("Cheats On");
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void drawAutosaveNotification() {
        using Clock = std::chrono::steady_clock;
        const std::int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count();
        if (nowNs >= autosaveNotificationUntilNs.load(std::memory_order_acquire)) {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 position(
            viewport->WorkPos.x + uiPixels(12.0f),
            viewport->WorkPos.y + uiPixels(12.0f));

        ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.72f);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, uiPixels(5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(uiPixels(8.0f), uiPixels(5.0f)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.025f, 0.03f, 0.04f, 1.0f));
        if (ImGui::Begin("##DoraemonSystemOverlayAutosave", nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 1.0f, 0.76f, 1.0f));
            ImGui::TextUnformatted("Autosave");
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    template <typename Enum, std::size_t Count>
    bool enumCombo(const char* label, Enum& value, const char* const (&labels)[Count]) {
        int selected = static_cast<int>(value);
        if (selected < 0 || selected >= static_cast<int>(Count)) {
            selected = 0;
        }

        bool changed = false;
        if (ImGui::BeginCombo(label, labels[selected])) {
            for (int index = 0; index < static_cast<int>(Count); index++) {
                const bool isSelected = selected == index;
                if (menuSelectable(labels[index], isSelected)) {
                    value = static_cast<Enum>(index);
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    template <typename Enum, std::size_t Count>
    bool mappedEnumCombo(
        const char* label,
        Enum& value,
        const Enum (&values)[Count],
        const char* const (&labels)[Count])
    {
        int selected = 0;
        for (int index = 0; index < static_cast<int>(Count); index++) {
            if (value == values[index]) {
                selected = index;
                break;
            }
        }

        bool changed = false;
        if (ImGui::BeginCombo(label, labels[selected])) {
            for (int index = 0; index < static_cast<int>(Count); index++) {
                const bool isSelected = value == values[index];
                if (menuSelectable(labels[index], isSelected)) {
                    value = values[index];
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    void setGameProfileValues(bool modern) {
        using namespace ultramodern::renderer;

        settings.autosave = modern;
        settings.modernCamera = modern;
        settings.graphics.rr_option = modern ? RefreshRate::Display : RefreshRate::Original;
#if defined(__ANDROID__)
        settings.graphics.res_option = modern ? Resolution::Original2x : Resolution::Original;
#else
        settings.graphics.res_option = modern ? Resolution::Auto : Resolution::Original;
#endif
        settings.graphics.ar_option = modern ? AspectRatio::Expand : AspectRatio::Original;
        settings.graphics.hr_option = modern ? HUDRatioMode::Full : HUDRatioMode::Original;
        settings.drawDistance = modern ? 5.0f : 1.0f;
        settings.modelLodDistance = modern ? 5.0f : 1.0f;
    }

    void applyGameProfile(bool modern) {
        setGameProfileValues(modern);
        settings.drawDistance = doraemon_draw_distance_configure(settings.drawDistance);
        settings.modelLodDistance = doraemon_model_lod_configure(settings.modelLodDistance);
        autosaveEnabled.store(settings.autosave, std::memory_order_release);
        doraemon::camera::configure(settings.modernCamera, settings.cameraStickSpeed,
            settings.cameraMouseSensitivity, settings.cameraInvertY, settings.cameraCaptureMouse, settings.cameraInvertX);
        applyGraphicsSettings(); // Apply graphics and save all profile values together.
    }

    void drawGameTab() {
        ImGui::TextUnformatted("Settings profiles");
        const float profileWidth =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        const ImVec2 profileSize(profileWidth, ImGui::GetFrameHeight() * 1.8f);
        if (ImGui::Button("Modern", profileSize)) {
            applyGameProfile(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Original", profileSize)) {
            applyGameProfile(false);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const auto& languages = doraemon::localization::available_languages();
        std::size_t selectedLanguage = doraemon::localization::language();
        if (selectedLanguage >= languages.size()) {
            selectedLanguage = 0;
        }

        if (ImGui::BeginCombo("Language", languages[selectedLanguage].name.c_str())) {
            for (std::size_t index = 0; index < languages.size(); index++) {
                const bool isSelected = index == selectedLanguage;
                ImGui::PushID(languages[index].code.c_str());
                if (menuSelectable(languages[index].name.c_str(), isSelected)) {
                    doraemon::localization::set_language(index);
                    settings.languageCode = languages[index].code;
                    saveSettings();
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Checkbox("Autosave", &settings.autosave)) {
            autosaveEnabled.store(settings.autosave, std::memory_order_release);
            saveSettings();
        }

        ImGui::Separator();
        bool cameraChanged = ImGui::Checkbox("Modern camera", &settings.modernCamera);
        if (settings.modernCamera) {
#if defined(__ANDROID__)
            ImGui::TextWrapped("Rotate with the right stick or swipe the free area on the right. Adjust swipe sensitivity in Touch. Scripted and boss cameras keep their original control.");
#else
            ImGui::TextWrapped("Rotate with the right stick or camera keys. Enable mouse capture to rotate with the mouse. Scripted and boss cameras retain their original control.");
#endif
            cameraChanged |= ImGui::SliderFloat("Camera stick speed", &settings.cameraStickSpeed, 30.0f, 360.0f, "%.0f deg/s");
#if !defined(__ANDROID__)
            cameraChanged |= ImGui::Checkbox("Capture mouse", &settings.cameraCaptureMouse);
            cameraChanged |= ImGui::SliderFloat("Camera mouse sensitivity", &settings.cameraMouseSensitivity, 0.02f, 1.0f, "%.2f deg/pixel");
#endif
            cameraChanged |= ImGui::Checkbox("Invert camera X", &settings.cameraInvertX);
            cameraChanged |= ImGui::Checkbox("Invert camera Y", &settings.cameraInvertY);
#if defined(__ANDROID__)
            ImGui::TextWrapped("Zoom: touch + / -, D-pad up / down, or hold Camera mode and move the camera stick vertically. The gear or Back / Select opens this menu.");
#else
            ImGui::TextWrapped("Zoom: D-pad up / down (your configured bindings), mouse wheel, Page Up / Page Down, or hold Camera mode (RB / E by default) and move the camera stick vertically.");
            ImGui::TextDisabled("Esc opens this menu and releases the mouse.");
#endif
        }
        if (cameraChanged) {
            doraemon::camera::configure(settings.modernCamera, settings.cameraStickSpeed,
                settings.cameraMouseSensitivity, settings.cameraInvertY, settings.cameraCaptureMouse, settings.cameraInvertX);
            saveSettings();
        }
        ImGui::Separator();

        if (ImGui::Checkbox("Show FPS counter", &settings.showFps)) {
            saveSettings();
        }
    }

    void drawCheatsTab() {
        bool changed = ImGui::Checkbox("Infinite HP", &settings.cheatHealth);
        ImGui::TextDisabled("Keep health full and prevent HP loss.");
        ImGui::Spacing();
        changed |= ImGui::Checkbox("Infinite lives (9)", &settings.cheatLives);
        ImGui::TextDisabled("Keep 9 lives, including after a failed attempt.");
        ImGui::Spacing();
        const bool racing = doraemon_cheats_race_active() != 0;
        ImGui::BeginDisabled(racing);
        changed |= ImGui::Checkbox("First-race torpedo speed", &settings.cheatTorpedo);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Use the first race's torpedo movement in all three races. Race order and rewards stay unchanged. Set this before the race starts.");
        if (racing) ImGui::TextDisabled("This setting can be changed after the race finishes.");
        if (changed) {
            doraemon_cheats_configure(settings.cheatHealth, settings.cheatLives, settings.cheatTorpedo);
            saveSettings();
        }
    }

    void drawGraphicsTab() {
        using namespace ultramodern::renderer;

        constexpr const char* windowModes[] = {
            "Windowed",
            "Borderless fullscreen"
        };
        constexpr Resolution resolutionValues[] = {
            Resolution::Original, Resolution::Original2x,
            Resolution::Original4x, Resolution::Auto
        };
        constexpr const char* resolutions[] = {
            "Original (1x)", "Original (2x)", "Original (4x)", "Fit to window"
        };
        constexpr AspectRatio aspectRatioValues[] = {
            AspectRatio::Original, AspectRatio::Expand
        };
        constexpr const char* aspectRatioLabels[] = {
            "Original (4:3)", "Fit to window / fullscreen"
        };
        constexpr HUDRatioMode hudLayoutValues[] = {
            HUDRatioMode::Original, HUDRatioMode::Full
        };
        constexpr const char* hudLayoutLabels[] = {
            "Original (4:3)", "Fit to window / fullscreen"
        };
        constexpr const char* antialiasing[] = {
            "None", "MSAA 2x", "MSAA 4x"
        };
        constexpr const char* frameRates[] = {
            "Original", "Display", "Manual"
        };
        constexpr const char* textureFiltering[] = {
            "Original N64 (three-point)", "Bilinear"
        };

        bool changed = false;
#if !defined(__ANDROID__)
        changed |= enumCombo("Display mode", settings.graphics.wm_option, windowModes);
        if (displays.empty()) {
            refreshDisplays();
        }
        changed |= displayCombo();

        if (const DisplayInfo* display = selectedDisplay()) {
            if (settings.graphics.wm_option != WindowMode::Windowed) {
                const OutputMode& desktopMode = display->desktopMode;
                if ((desktopMode.width > 0) && (desktopMode.height > 0)) {
                    ImGui::Text("Output resolution: %s (desktop/native)",
                        outputModeLabel(
                            desktopMode.width,
                            desktopMode.height,
                            desktopMode.refreshRate,
                            true).c_str());
                }
            }
            else {
                changed |= outputModeCombo(*display, false);
            }
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            refreshDisplays();
        }

#endif
        changed |= enumCombo("Frame rate", settings.graphics.rr_option, frameRates);
        if (settings.graphics.rr_option == RefreshRate::Manual) {
            changed |= ImGui::SliderInt("Frame rate limit", &settings.graphics.rr_manual_value,
                60, 360, "%d FPS", ImGuiSliderFlags_AlwaysClamp);
        }
        if (settings.graphics.rr_option != RefreshRate::Original) {
            const unsigned displayRate = ultramodern::get_display_refresh_rate();
            if (displayRate > 0) {
                const unsigned targetRate = settings.graphics.rr_option == RefreshRate::Display ?
                    displayRate : std::min(displayRate, unsigned(settings.graphics.rr_manual_value));
                ImGui::Text("Display: %u Hz | Target: %u FPS", displayRate, targetRate);
            }
            ImGui::TextWrapped("Smoother motion with the original game speed. Maximum FPS follows the display refresh rate.");
        }

        changed |= mappedEnumCombo(
            "Game render resolution", settings.graphics.res_option,
            resolutionValues, resolutions);
        changed |= mappedEnumCombo(
            "Game aspect ratio",
            settings.graphics.ar_option,
            aspectRatioValues,
            aspectRatioLabels);
        changed |= mappedEnumCombo(
            "Gameplay HUD layout",
            settings.graphics.hr_option,
            hudLayoutValues,
            hudLayoutLabels);
        changed |= enumCombo("Anti-aliasing", settings.graphics.msaa_option, antialiasing);
        changed |= enumCombo(
            "Texture filtering", settings.graphics.tf_option, textureFiltering);

        if (changed) {
            applyGraphicsSettings();
        }
        ImGui::Spacing();
        if (ImGui::SliderFloat("Object / NPC draw distance", &settings.drawDistance,
                1.0f, 5.0f, "%.1fx", ImGuiSliderFlags_AlwaysClamp)) {
            settings.drawDistance = doraemon_draw_distance_configure(settings.drawDistance);
            saveSettings();
        }
        ImGui::TextDisabled("1x = Original. Higher values show objects farther away.");
        ImGui::Spacing();
        if (ImGui::SliderFloat("Character LOD distance", &settings.modelLodDistance,
                1.0f, 5.0f, "%.1fx", ImGuiSliderFlags_AlwaysClamp)) {
            settings.modelLodDistance = doraemon_model_lod_configure(settings.modelLodDistance);
            saveSettings();
        }
        ImGui::TextDisabled("1x = Original. Higher values keep character detail farther away.");
    }

    void drawAudioTab() {
        int volumePercent = static_cast<int>(settings.masterVolume * 100.0f + 0.5f);
        if (ImGui::SliderInt("Master volume", &volumePercent, 0, 100, "%d%%")) {
            settings.masterVolume = static_cast<float>(volumePercent) / 100.0f;
            doraemon::audio::set_master_volume(settings.masterVolume);
            saveSettings();
        }
    }

    void drawControlsTab() {
        using doraemon::input::Action;
        using doraemon::input::BindingDevice;
        using doraemon::input::GamepadInput;

        if (doraemon::input::is_gamepad_connected()) {
            const std::string name = doraemon::input::gamepad_name();
            ImGui::Text("Gamepad: %s", name.empty() ? "SDL controller" : name.c_str());
        }
        else {
            ImGui::TextDisabled("Gamepad: not connected");
        }

#if !defined(__ANDROID__)
        bool backgroundGamepadInput =
            doraemon::input::is_background_gamepad_input_enabled();
        if (ImGui::Checkbox("Background gamepad input", &backgroundGamepadInput)) {
            doraemon::input::set_background_gamepad_input_enabled(backgroundGamepadInput);
        }
        ImGui::TextDisabled("Gamepad only; keyboard input still requires focus.");
#endif

        int stickDeadZonePercent = doraemon::input::stick_dead_zone_percent();
        if (ImGui::SliderInt(
                "Stick dead zone",
                &stickDeadZonePercent,
                doraemon::input::MinimumStickDeadZonePercent,
                doraemon::input::MaximumStickDeadZonePercent,
                "%d%%")) {
            doraemon::input::set_stick_dead_zone_percent(stickDeadZonePercent);
        }
#if defined(__ANDROID__)
        ImGui::TextDisabled("Back / Select opens the port menu. Touch controls are in the Touch tab.");
#else
        ImGui::TextDisabled("Escape and Back / Select are reserved for the PC menu.");
#endif
        ImGui::Spacing();

        bool openCapturePopup = false;
        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp;
#if defined(__ANDROID__)
        constexpr int bindingColumns = 2;
#else
        constexpr int bindingColumns = 3;
#endif
        if (ImGui::BeginTable("##InputBindings", bindingColumns, tableFlags, ImVec2(0.0f, uiPixels(252.0f)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 2.0f);
#if !defined(__ANDROID__)
            ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.0f);
#endif
            ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();

            for (int index = 0; index < static_cast<int>(Action::Count); index++) {
                const Action action = static_cast<Action>(index);
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(doraemon::input::action_name(action));

#if !defined(__ANDROID__)
                ImGui::TableSetColumnIndex(1);
                const std::string keyboard = doraemon::input::keyboard_binding_name(action);
                if (ImGui::Button(keyboard.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                    captureAction = action;
                    captureDevice = BindingDevice::Keyboard;
                    doraemon::input::begin_binding_capture(action, captureDevice);
                    openCapturePopup = true;
                }

#endif
                ImGui::TableSetColumnIndex(bindingColumns - 1);
                const std::string gamepad = doraemon::input::gamepad_binding_name(action);
                if (ImGui::Button(gamepad.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                    captureAction = action;
                    captureDevice = BindingDevice::Gamepad;
                    doraemon::input::begin_binding_capture(action, captureDevice);
                    openCapturePopup = true;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (openCapturePopup) {
            ImGui::OpenPopup("Bind control");
        }
        ImGui::SetNextWindowPos(
            ImGui::GetMainViewport()->GetCenter(),
            ImGuiCond_Always,
            ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Bind control", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(doraemon::input::action_name(captureAction));
            ImGui::Spacing();
            if (doraemon::input::is_binding_capture_active()) {
                ImGui::TextWrapped(
                    captureDevice == BindingDevice::Keyboard
                        ? "Release the current key, then press the new key."
                        : "Release the current control, then press a button, trigger, or stick direction.");
            }
            else {
                ImGui::CloseCurrentPopup();
                captureAction = Action::Count;
            }

            ImGui::Spacing();
            if (ImGui::Button("Clear binding", ImVec2(uiPixels(120.0f), 0.0f))) {
                if (captureDevice == BindingDevice::Keyboard) {
                    doraemon::input::set_keyboard_binding(captureAction, SDL_SCANCODE_UNKNOWN);
                }
                else {
                    doraemon::input::set_gamepad_binding(captureAction, GamepadInput::None);
                }
                doraemon::input::cancel_binding_capture();
                ImGui::CloseCurrentPopup();
                captureAction = Action::Count;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(uiPixels(100.0f), 0.0f))) {
                doraemon::input::cancel_binding_capture();
                ImGui::CloseCurrentPopup();
                captureAction = Action::Count;
            }
            ImGui::EndPopup();
        }

        if (ImGui::Button("Restore default controls")) {
            doraemon::input::reset_bindings();
        }
    }

    void drawTabContent(const char* id, void (*drawContent)()) {
#if defined(__ANDROID__)
        // Keep both the tab bar and footer fixed; only this tab's body scrolls.
        if (ImGui::BeginChild(id, ImVec2(0, -uiPixels(72.0f)))) drawContent();
        ImGui::EndChild();
#else
        drawContent();
#endif
    }

    void drawMenu() {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        ImGui::SetNextFrameWantCaptureMouse(true);

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* background = ImGui::GetBackgroundDrawList();
        background->AddRectFilled(
            viewport->Pos,
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
            IM_COL32(0, 0, 0, 145));

        const float menuWidth = std::max(1.0f,
            std::min(viewport->WorkSize.x - uiPixels(48.0f), uiPixels(760.0f)));
#if defined(__ANDROID__)
        constexpr float menuHeightUnits = 620.0f;
#else
        constexpr float menuHeightUnits = 500.0f;
#endif
        const float menuHeight = std::max(1.0f,
            std::min(viewport->WorkSize.y - uiPixels(48.0f), uiPixels(menuHeightUnits)));
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(menuWidth, menuHeight), ImGuiCond_Always);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, uiPixels(10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(uiPixels(22.0f), uiPixels(18.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, uiPixels(5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, uiPixels(5.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.025f, 0.035f, 0.055f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.05f, 0.18f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.38f, 0.56f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.11f, 0.52f, 0.72f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.45f, 0.95f, 0.75f, 1.0f));

#if defined(__ANDROID__)
        // Larger hit areas for fingers, scaled with the existing 720p UI baseline.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(uiPixels(10.0f), uiPixels(14.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(uiPixels(10.0f), uiPixels(10.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, uiPixels(24.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, uiPixels(28.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
#endif
        if (ImGui::Begin("##DoraemonPcMenu", nullptr, flags)) {
            ImGui::SetWindowFontScale(1.18f);
            ImGui::TextUnformatted("Dora64");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SameLine();
#if defined(__ANDROID__)
            ImGui::TextDisabled("Port menu");
#else
            ImGui::TextDisabled("PC menu");
#endif
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginTabBar("##DoraemonPcMenuTabs")) {
                if (ImGui::BeginTabItem("Game")) {
                    drawTabContent("##PortGameContent", drawGameTab);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Graphics")) {
                    drawTabContent("##PortGraphicsContent", drawGraphicsTab);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Audio")) {
                    drawTabContent("##PortAudioContent", drawAudioTab);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Controls")) {
                    drawTabContent("##PortControlsContent", drawControlsTab);
                    ImGui::EndTabItem();
                }
#if defined(__ANDROID__)
                if (ImGui::BeginTabItem("Touch")) {
                    drawTabContent("##PortTouchContent", doraemon::touch::draw_settings);
                    ImGui::EndTabItem();
                }
#endif
                if (ImGui::BeginTabItem("Cheats")) {
                    drawTabContent("##PortCheatsContent", drawCheatsTab);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

#if !defined(__ANDROID__)
            const float footerY = ImGui::GetWindowHeight() - uiPixels(54.0f);
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), footerY));
#endif
            ImGui::Separator();
#if defined(__ANDROID__)
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Back / Select: close menu");
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                uiPixels(200.0f) - ImGui::GetStyle().ItemSpacing.x);
#else
            ImGui::TextDisabled("Escape / Back: close menu");
            ImGui::SameLine(ImGui::GetWindowWidth() - uiPixels(330.0f));
#endif
            if (ImGui::Button("Continue", ImVec2(uiPixels(100.0f), 0.0f))) {
                doraemon::system_overlay::toggle_menu();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset game", ImVec2(uiPixels(100.0f), 0.0f))) {
                ImGui::OpenPopup("Reset game?");
            }
#if !defined(__ANDROID__)
            ImGui::SameLine();
            if (ImGui::Button("Exit game", ImVec2(uiPixels(100.0f), 0.0f))) {
                ImGui::OpenPopup("Exit game?");
            }
#endif

            ImGui::SetNextWindowPos(
                ImGui::GetMainViewport()->GetCenter(),
                ImGuiCond_Always,
                ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Reset game?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Return to the opening screen?");
                ImGui::TextDisabled("Unsaved progress will be lost.");
                ImGui::Spacing();
                if (ImGui::Button("Reset", ImVec2(uiPixels(100.0f), 0.0f))) {
                    std::puts("Game reset selected in PC menu");
                    gameResetRequested.store(true, std::memory_order_release);
                    menuOpen.store(false, std::memory_order_release);
                    doraemon::input::cancel_binding_capture();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(uiPixels(100.0f), 0.0f))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

#if !defined(__ANDROID__)
            ImGui::SetNextWindowPos(
                ImGui::GetMainViewport()->GetCenter(),
                ImGuiCond_Always,
                ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Exit game?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Exit Dora64?");
                ImGui::Spacing();
                if (ImGui::Button("Exit", ImVec2(uiPixels(100.0f), 0.0f))) {
                    ultramodern::quit();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(uiPixels(100.0f), 0.0f))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
#endif
        }
#if defined(__ANDROID__)
        const ImVec2 menuPos = ImGui::GetWindowPos(), menuSize = ImGui::GetWindowSize();
        const bool canDismiss = !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
        doraemon::touch::set_port_menu_bounds({
            (menuPos.x - viewport->Pos.x) / viewport->Size.x,
            (menuPos.y - viewport->Pos.y) / viewport->Size.y,
            menuSize.x / viewport->Size.x, menuSize.y / viewport->Size.y}, canDismiss);
        // Real mouse clicks use ImGui; touchscreen dismissal is handled by the
        // raw finger event so that same contact cannot press a game control.
        const ImVec2 mouse = io.MousePos;
        const bool outside = mouse.x < menuPos.x || mouse.y < menuPos.y ||
            mouse.x > menuPos.x + menuSize.x || mouse.y > menuPos.y + menuSize.y;
        if (canDismiss && outside && io.MouseSource != ImGuiMouseSource_TouchScreen &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && doraemon::system_overlay::is_menu_open())
            doraemon::system_overlay::toggle_menu();
#endif
        ImGui::End();
#if defined(__ANDROID__)
        ImGui::PopStyleVar(5);
#endif

        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(4);
    }
}

void doraemon::system_overlay::initialize(const std::filesystem::path& configDirectory) {
    settingsPath = configDirectory / "doraemon_pc_settings.json";
    settings.graphics = ultramodern::renderer::get_graphics_config();
    settings.graphics.rr_option = ultramodern::renderer::RefreshRate::Original;
    settings.graphics.rr_manual_value = 144;
    doraemon::localization::initialize();
    // A saved language, including original Japanese, overrides the OS default.
    settings.languageCode = systemLanguageCode();
    std::error_code settingsError;
    const bool firstRun = !std::filesystem::exists(settingsPath, settingsError) && !settingsError;
    if (firstRun) {
        setGameProfileValues(true);
        settings.showFps = false;
    }
    loadSettings();
#if defined(__ANDROID__)
    std::fprintf(stderr,
        "Dora64 graphics settings: resolution=%d downsample=%d refresh=%d msaa=%d drawDistance=%.2f aspect=%d\n",
        static_cast<int>(settings.graphics.res_option), settings.graphics.ds_option,
        static_cast<int>(settings.graphics.rr_option),
        static_cast<int>(settings.graphics.msaa_option),
        settings.drawDistance, static_cast<int>(settings.graphics.ar_option));
    std::fflush(stderr);
    // The single Android surface always occupies the device display.
    settings.graphics.wm_option = ultramodern::renderer::WindowMode::Fullscreen;
#endif
    doraemon::camera::configure(settings.modernCamera, settings.cameraStickSpeed,
        settings.cameraMouseSensitivity, settings.cameraInvertY, settings.cameraCaptureMouse, settings.cameraInvertX);
    autosaveEnabled.store(settings.autosave, std::memory_order_release);
    settings.drawDistance = doraemon_draw_distance_configure(settings.drawDistance);
    settings.modelLodDistance = doraemon_model_lod_configure(settings.modelLodDistance);
    doraemon_cheats_configure(settings.cheatHealth, settings.cheatLives, settings.cheatTorpedo);

    // RT64 exposes an 8x enum value, but it is not reliable on every backend
    // and currently crashes the Windows renderer on the tested hardware.
    if (settings.graphics.msaa_option == ultramodern::renderer::Antialiasing::MSAA8X) {
        settings.graphics.msaa_option = ultramodern::renderer::Antialiasing::MSAA4X;
        saveSettings();
    }

    doraemon::audio::set_master_volume(settings.masterVolume);
    const std::size_t language =
        doraemon::localization::find_language(settings.languageCode);
    doraemon::localization::set_language(language);
    settings.languageCode = doraemon::localization::current_language().code;
    ultramodern::renderer::set_graphics_config(settings.graphics);
    if (firstRun) {
        saveSettings();
    }
}

extern "C" int doraemon_collectible_autosave_enabled() {
    return autosaveEnabled.load(std::memory_order_acquire) ? 1 : 0;
}

extern "C" void doraemon_show_autosave_notification() {
    using Clock = std::chrono::steady_clock;
    const std::int64_t untilNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch() + std::chrono::seconds(2)).count();
    autosaveNotificationUntilNs.store(untilNs, std::memory_order_release);
}

bool doraemon::system_overlay::is_menu_open() {
    return menuOpen.load(std::memory_order_acquire);
}

extern "C" int doraemon_game_reset_requested() {
    return gameResetRequested.load(std::memory_order_acquire) ? 1 : 0;
}

extern "C" void doraemon_clear_game_reset_request() {
    gameResetRequested.store(false, std::memory_order_release);
}

void doraemon::system_overlay::toggle_menu() {
    bool current = menuOpen.load(std::memory_order_acquire);
    while (!menuOpen.compare_exchange_weak(
        current,
        !current,
        std::memory_order_acq_rel,
        std::memory_order_acquire)) {
    }

    doraemon::touch::cancel();
    doraemon::camera::clear_input();
    if (current) {
        doraemon::input::cancel_binding_capture();
    }
}

void doraemon::system_overlay::update_input() {
    using doraemon::input::GamepadInput;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    const bool connected = doraemon::input::is_gamepad_connected();
    if (connected) {
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    }
    else {
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    }

    const auto value = [](GamepadInput input) {
        return doraemon::input::gamepad_input_value(input);
    };
    const auto button = [&](ImGuiKey key, GamepadInput input) {
        io.AddKeyEvent(key, connected && (value(input) >= 0.5f));
    };
    const auto analog = [&](ImGuiKey key, GamepadInput input) {
        const float amount = connected ? value(input) : 0.0f;
        io.AddKeyAnalogEvent(key, amount >= 0.1f, amount);
    };

    button(ImGuiKey_GamepadStart, GamepadInput::Start);
    button(ImGuiKey_GamepadFaceLeft, GamepadInput::X);
    button(ImGuiKey_GamepadFaceRight, GamepadInput::B);
    button(ImGuiKey_GamepadFaceUp, GamepadInput::Y);
    button(ImGuiKey_GamepadFaceDown, GamepadInput::A);
    button(ImGuiKey_GamepadDpadLeft, GamepadInput::DpadLeft);
    button(ImGuiKey_GamepadDpadRight, GamepadInput::DpadRight);
    button(ImGuiKey_GamepadDpadUp, GamepadInput::DpadUp);
    button(ImGuiKey_GamepadDpadDown, GamepadInput::DpadDown);
    button(ImGuiKey_GamepadL1, GamepadInput::LeftShoulder);
    button(ImGuiKey_GamepadR1, GamepadInput::RightShoulder);
    analog(ImGuiKey_GamepadL2, GamepadInput::LeftTrigger);
    analog(ImGuiKey_GamepadR2, GamepadInput::RightTrigger);
    button(ImGuiKey_GamepadL3, GamepadInput::LeftStickButton);
    button(ImGuiKey_GamepadR3, GamepadInput::RightStickButton);
    analog(ImGuiKey_GamepadLStickLeft, GamepadInput::LeftStickLeft);
    analog(ImGuiKey_GamepadLStickRight, GamepadInput::LeftStickRight);
    analog(ImGuiKey_GamepadLStickUp, GamepadInput::LeftStickUp);
    analog(ImGuiKey_GamepadLStickDown, GamepadInput::LeftStickDown);
    analog(ImGuiKey_GamepadRStickLeft, GamepadInput::RightStickLeft);
    analog(ImGuiKey_GamepadRStickRight, GamepadInput::RightStickRight);
    analog(ImGuiKey_GamepadRStickUp, GamepadInput::RightStickUp);
    analog(ImGuiKey_GamepadRStickDown, GamepadInput::RightStickDown);
}

void doraemon::system_overlay::record_output_frame() {
    outputFrames.fetch_add(1, std::memory_order_relaxed);
}

void doraemon::system_overlay::draw() {
    const ScopedOverlayScale scaledOverlay;
    const float fps = fpsCounter.nextFrame(outputFrames.exchange(0, std::memory_order_relaxed));

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (doraemon::input::is_binding_capture_active()) {
            doraemon::input::cancel_binding_capture();
            captureAction = doraemon::input::Action::Count;
        }
        else {
            toggle_menu();
        }
    }

    if (is_menu_open()) {
        drawMenu();
    }

    doraemon::touch::draw();

    if (settings.showFps) {
        drawFps(fps);
    }

    drawAutosaveNotification();
    if (doraemon_cheats_any_enabled()) drawCheatsIndicator();
}
