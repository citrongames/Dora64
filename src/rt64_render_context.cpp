#if defined(_WIN32)
#include <Windows.h>
#endif

#include "rt64_render_context.hpp"
#include "android_cpu_probe.hpp"

#if defined(__ANDROID__)
#include <SDL.h>
#include <android/log.h>
#include <jni.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "hle/rt64_application.h"
#include "render/rt64_texture_cache.h"
#include "librecomp/game.hpp"
#include "doraemon_localization.hpp"
#include "doraemon_lighting.hpp"
#include "doraemon_sky.hpp"
#include "doraemon_camera.hpp"
#include "doraemon_cheats.h"
#include "doraemon_model_pool.h"
#include "gbi/rt64_gbi_f3d.h"
#include "rhi/rt64_render_hooks.h"
#include "system_overlay.hpp"
#include "ultramodern/config.hpp"

namespace {
#if defined(__ANDROID__)
    std::atomic_bool shaderLoadingVisible{ false };
    void show_android_shader_loading(bool visible) {
        auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
        auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !activity) return;
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(activityClass, "setShaderCompilationVisible", "(Z)V");
        if (method) env->CallVoidMethod(activity, method, visible ? JNI_TRUE : JNI_FALSE);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            __android_log_print(ANDROID_LOG_WARN, "Dora64Native", "Could not update shader compilation message");
        }
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
    }

    void android_trace(const char* message, bool reset = false) {
        __android_log_print(ANDROID_LOG_INFO, "Dora64Native", "%s", message);
        const auto path = recomp::get_config_path() / "android-trace.log";
        if (FILE* file = std::fopen(path.c_str(), reset ? "w" : "a")) {
            std::fprintf(file, "%s\n", message);
            std::fflush(file);
            fsync(fileno(file));
            std::fclose(file);
        }
    }
#endif

    RT64::RenderHookDraw* previousOutputDraw = nullptr;
    void count_output_frame(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
        doraemon::cpu_probe::register_present();
#if defined(__ANDROID__)
        static std::atomic_bool first_output_before{false};
        if (!first_output_before.exchange(true)) android_trace("first output draw begin");
#endif
        if (previousOutputDraw) previousOutputDraw(list, framebuffer);
        doraemon::system_overlay::record_output_frame();
#if defined(__ANDROID__)
        static std::atomic_bool first_output_after{false};
        if (!first_output_after.exchange(true)) android_trace("first output draw complete");
        if (shaderLoadingVisible.exchange(false)) show_android_shader_loading(false);
#endif
    }

    void model_pool_matrix(RT64::State* state, RT64::DisplayList** dl) {
        if (!doraemon_model_pool_matrix_address((*dl)->w1)) {
            RT64::GBI_F3D::matrix(state, dl);
            return;
        }
        // Only relocated model matrices bypass segmented N64 addressing.
        // Restore the prior mode immediately so textures and sky stay original.
        const bool previous = state->extended.extendRDRAM;
        state->setExtendedRDRAM(true);
        RT64::GBI_F3D::matrix(state, dl);
        state->setExtendedRDRAM(previous);
    }

    uint8_t rt64_dmem[0x1000]{};
    uint8_t rt64_imem[0x1000]{};
    uint8_t dummy_rom_header[0x40]{};

    uint32_t mi_intr_reg = 0;
    uint32_t dpc_start_reg = 0;
    uint32_t dpc_end_reg = 0;
    uint32_t dpc_current_reg = 0;
    uint32_t dpc_status_reg = 0;
    uint32_t dpc_clock_reg = 0;
    uint32_t dpc_bufbusy_reg = 0;
    uint32_t dpc_pipebusy_reg = 0;
    uint32_t dpc_tmem_reg = 0;

    void check_interrupts() {
    }

    RT64::UserConfiguration::AspectRatio to_rt64(
        ultramodern::renderer::AspectRatio value)
    {
        using Input = ultramodern::renderer::AspectRatio;
        using Output = RT64::UserConfiguration::AspectRatio;
        switch (value) {
            case Input::Original: return Output::Original;
            case Input::Expand: return Output::Expand;
            case Input::Manual: return Output::Manual;
            case Input::OptionCount: break;
        }
        return Output::Original;
    }

    RT64::UserConfiguration::Antialiasing to_rt64(
        ultramodern::renderer::Antialiasing value)
    {
        using Input = ultramodern::renderer::Antialiasing;
        using Output = RT64::UserConfiguration::Antialiasing;
        switch (value) {
            case Input::None: return Output::None;
            case Input::MSAA2X: return Output::MSAA2X;
            case Input::MSAA4X: return Output::MSAA4X;
            case Input::MSAA8X: return Output::MSAA8X;
            case Input::OptionCount: break;
        }
        return Output::None;
    }

    RT64::UserConfiguration::RefreshRate to_rt64(
        ultramodern::renderer::RefreshRate value)
    {
        using Input = ultramodern::renderer::RefreshRate;
        using Output = RT64::UserConfiguration::RefreshRate;
        switch (value) {
            case Input::Original: return Output::Original;
            case Input::Display: return Output::Display;
            case Input::Manual: return Output::Manual;
            case Input::OptionCount: break;
        }
        return Output::Original;
    }

    RT64::UserConfiguration::InternalColorFormat to_rt64(
        ultramodern::renderer::HighPrecisionFramebuffer value)
    {
        using Input = ultramodern::renderer::HighPrecisionFramebuffer;
        using Output = RT64::UserConfiguration::InternalColorFormat;
        switch (value) {
            case Input::Auto: return Output::Automatic;
            case Input::On: return Output::High;
            case Input::Off: return Output::Standard;
            case Input::OptionCount: break;
        }
        return Output::Automatic;
    }

    RT64::ApplicationWindow::DisplayMode to_rt64(
        ultramodern::renderer::WindowMode value)
    {
        using Input = ultramodern::renderer::WindowMode;
        using Output = RT64::ApplicationWindow::DisplayMode;
        switch (value) {
            case Input::Windowed: return Output::Windowed;
            case Input::Fullscreen: return Output::BorderlessFullscreen;
            case Input::OptionCount: break;
        }
        return Output::Windowed;
    }

    void apply_user_config(
        RT64::Application& application,
        const ultramodern::renderer::GraphicsConfig& config)
    {
        const int downsample = std::max(config.ds_option, 1);
        switch (config.res_option) {
            case ultramodern::renderer::Resolution::Auto:
                application.userConfig.resolution =
                    RT64::UserConfiguration::Resolution::WindowIntegerScale;
                application.userConfig.downsampleMultiplier = 1;
                break;
            case ultramodern::renderer::Resolution::Original:
                application.userConfig.resolution =
                    RT64::UserConfiguration::Resolution::Manual;
                application.userConfig.resolutionMultiplier = downsample;
                application.userConfig.downsampleMultiplier = downsample;
                break;
            case ultramodern::renderer::Resolution::Original2x:
                application.userConfig.resolution =
                    RT64::UserConfiguration::Resolution::Manual;
                application.userConfig.resolutionMultiplier = 2.0f * downsample;
                application.userConfig.downsampleMultiplier = downsample;
                break;
            case ultramodern::renderer::Resolution::Original4x:
                application.userConfig.resolution =
                    RT64::UserConfiguration::Resolution::Manual;
                application.userConfig.resolutionMultiplier = 4.0f * downsample;
                application.userConfig.downsampleMultiplier = downsample;
                break;
            case ultramodern::renderer::Resolution::OptionCount:
                break;
        }

        switch (config.hr_option) {
            case ultramodern::renderer::HUDRatioMode::Original:
                application.userConfig.extAspectRatio =
                    RT64::UserConfiguration::AspectRatio::Original;
                break;
            case ultramodern::renderer::HUDRatioMode::Clamp16x9:
                application.userConfig.extAspectRatio =
                    RT64::UserConfiguration::AspectRatio::Manual;
                application.userConfig.extAspectTarget = 16.0f / 9.0f;
                break;
            case ultramodern::renderer::HUDRatioMode::Full:
                application.userConfig.extAspectRatio =
                    RT64::UserConfiguration::AspectRatio::Expand;
                break;
            case ultramodern::renderer::HUDRatioMode::OptionCount:
                break;
        }

        application.userConfig.aspectRatio = to_rt64(config.ar_option);
        application.userConfig.antialiasing = to_rt64(config.msaa_option);
        application.userConfig.threePointFiltering =
            config.tf_option == ultramodern::renderer::TextureFiltering::ThreePoint;
        application.userConfig.refreshRate = to_rt64(config.rr_option);
        application.userConfig.refreshRateTarget = config.rr_manual_value;
        application.userConfig.internalColorFormat = to_rt64(config.hpfb_option);
        application.userConfig.displayBuffering =
            RT64::UserConfiguration::DisplayBuffering::Triple;
    }

    ultramodern::renderer::SetupResult map_setup_result(
        RT64::Application::SetupResult value)
    {
        using Input = RT64::Application::SetupResult;
        using Output = ultramodern::renderer::SetupResult;
        switch (value) {
            case Input::Success: return Output::Success;
            case Input::DynamicLibrariesNotFound:
                return Output::DynamicLibrariesNotFound;
            case Input::InvalidGraphicsAPI: return Output::InvalidGraphicsAPI;
            case Input::GraphicsAPINotFound: return Output::GraphicsAPINotFound;
            case Input::GraphicsDeviceNotFound: return Output::GraphicsDeviceNotFound;
            case Input::GraphicsPipelineCreationFailed: return Output::GraphicsPipelineCreationFailed;
        }
        std::abort();
    }

    ultramodern::renderer::GraphicsApi map_graphics_api(
        RT64::UserConfiguration::GraphicsAPI value)
    {
        using Input = RT64::UserConfiguration::GraphicsAPI;
        using Output = ultramodern::renderer::GraphicsApi;
        switch (value) {
            case Input::Automatic: return Output::Auto;
            case Input::D3D12: return Output::D3D12;
            case Input::Vulkan: return Output::Vulkan;
            case Input::Metal: return Output::Metal;
            case Input::OptionCount: break;
        }
        return Output::Auto;
    }
}

doraemon::renderer::RT64Context::RT64Context(
    uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode)
{
#if defined(__ANDROID__)
    android_trace("RT64 context begin", true);
#endif
    RT64::Application::Core core{};
#if defined(_WIN32)
    core.window = window_handle.window;
#elif defined(__ANDROID__)
    core.window = window_handle;
#else
    core.window = window_handle;
#endif
    core.checkInterrupts = check_interrupts;
    core.HEADER = dummy_rom_header;
    core.RDRAM = rdram;
    core.DMEM = rt64_dmem;
    core.IMEM = rt64_imem;
    core.MI_INTR_REG = &mi_intr_reg;
    core.DPC_START_REG = &dpc_start_reg;
    core.DPC_END_REG = &dpc_end_reg;
    core.DPC_CURRENT_REG = &dpc_current_reg;
    core.DPC_STATUS_REG = &dpc_status_reg;
    core.DPC_CLOCK_REG = &dpc_clock_reg;
    core.DPC_BUFBUSY_REG = &dpc_bufbusy_reg;
    core.DPC_PIPEBUSY_REG = &dpc_pipebusy_reg;
    core.DPC_TMEM_REG = &dpc_tmem_reg;

    auto* vi = ultramodern::renderer::get_vi_regs();
    core.VI_STATUS_REG = &vi->VI_STATUS_REG;
    core.VI_ORIGIN_REG = &vi->VI_ORIGIN_REG;
    core.VI_WIDTH_REG = &vi->VI_WIDTH_REG;
    core.VI_INTR_REG = &vi->VI_INTR_REG;
    core.VI_V_CURRENT_LINE_REG = &vi->VI_V_CURRENT_LINE_REG;
    core.VI_TIMING_REG = &vi->VI_TIMING_REG;
    core.VI_V_SYNC_REG = &vi->VI_V_SYNC_REG;
    core.VI_H_SYNC_REG = &vi->VI_H_SYNC_REG;
    core.VI_LEAP_REG = &vi->VI_LEAP_REG;
    core.VI_H_START_REG = &vi->VI_H_START_REG;
    core.VI_V_START_REG = &vi->VI_V_START_REG;
    core.VI_V_BURST_REG = &vi->VI_V_BURST_REG;
    core.VI_X_SCALE_REG = &vi->VI_X_SCALE_REG;
    core.VI_Y_SCALE_REG = &vi->VI_Y_SCALE_REG;

    RT64::ApplicationConfiguration app_config{};
    app_config.appId = "dora64";
    app_config.useConfigurationFile = false;
#if defined(__ANDROID__)
    // Android's HOME may point at /data, which an app cannot write to.
    // Keep RT64's log and cache under Dora64's private storage instead.
    app_config.detectDataPath = false;
    app_config.dataPath = recomp::get_config_path() / "rt64";
#endif
    app_config.updateOverlayInput = doraemon::system_overlay::update_input;
    app_config.drawOverlay = doraemon::system_overlay::draw;
#if defined(__ANDROID__)
    android_trace("RT64 application create begin");
#endif
    app = std::make_unique<RT64::Application>(core, app_config);
#if defined(__ANDROID__)
    android_trace("RT64 application created");
#endif

    const auto& config = ultramodern::renderer::get_graphics_config();
    apply_user_config(*app, config);
    app->userConfig.developerMode = developer_mode;
#if defined(__linux__)
    // RT64's optional idle-work thread continuously submits a dummy compute
    // dispatch to keep desktop GPUs from downclocking. WSL's dzn Vulkan layer
    // can hang inside that dispatch when the overlay starts producing larger
    // frames, which stalls rendering and eventually the emulated scheduler.
    // Normal workloads do not depend on this performance-only workaround.
    app->userConfig.idleWorkActive = false;
#endif
    app->enhancementConfig.f3dex.forceBranch = true;
    app->enhancementConfig.textureLOD.scale = true;

    switch (config.api_option) {
        case ultramodern::renderer::GraphicsApi::D3D12:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
            break;
        case ultramodern::renderer::GraphicsApi::Vulkan:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
            break;
        case ultramodern::renderer::GraphicsApi::Metal:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal;
            break;
        case ultramodern::renderer::GraphicsApi::Auto:
        case ultramodern::renderer::GraphicsApi::OptionCount:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic;
            break;
    }

    // Install before RT64 starts its workers. This hook runs for every output
    // frame, including interpolated frames; drawOverlay runs per source workload.
    // The static callback does not retain this context across shutdown/reset.
    if (RT64::GetRenderHookDraw() != &count_output_frame) {
        previousOutputDraw = RT64::GetRenderHookDraw();
        RT64::SetRenderHooks(RT64::GetRenderHookInit(), &count_output_frame,
            RT64::GetRenderHookDeinit());
    }
#if defined(__ANDROID__)
    shaderLoadingVisible.store(true);
    show_android_shader_loading(true);
    android_trace("RT64 setup begin");
#endif
    setup_result = map_setup_result(app->setup(0));
#if defined(__ANDROID__)
    android_trace("RT64 setup returned");
#endif
    chosen_api = map_graphics_api(app->chosenGraphicsAPI);
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
#if defined(__ANDROID__)
        if (shaderLoadingVisible.exchange(false)) show_android_shader_loading(false);
#endif
        std::fprintf(stderr, "RT64 setup failed (%d)\n", static_cast<int>(setup_result));
        app.reset();
        return;
    }

#if defined(__ANDROID__)
    std::fprintf(stderr, "Dora64 swapchain: %u x %u\n",
        app->sharedQueueResources->swapChainWidth,
        app->sharedQueueResources->swapChainHeight);
    std::fflush(stderr);
#endif
#if defined(__ANDROID__)
    // Android owns the native surface size. Applying the desktop 1280x720
    // window configuration changes SDL's logical size independently of the
    // Vulkan swap chain, so leave the SDL window at its surface dimensions.
    if (SDL_Window* window = SDL_GetWindowFromID(1)) {
        int window_width = 0;
        int window_height = 0;
        SDL_GetWindowSize(window, &window_width, &window_height);
        __android_log_print(ANDROID_LOG_INFO, "Dora64Native",
            "window SDL=%dx%d swapchain=%ux%u",
            window_width, window_height,
            app->sharedQueueResources->swapChainWidth,
            app->sharedQueueResources->swapChainHeight);
    }
#else
    app->setDisplayConfig(
        to_rt64(config.wm_option),
        config.display_index,
        config.display_width,
        config.display_height,
        config.display_refresh_rate);
#endif
    sync_localized_textures();
    std::printf("RT64 renderer initialized\n");
}

doraemon::renderer::RT64Context::~RT64Context() {
#if defined(__ANDROID__)
    if (shaderLoadingVisible.exchange(false)) show_android_shader_loading(false);
#endif
}

bool doraemon::renderer::RT64Context::valid() {
    return static_cast<bool>(app);
}

bool doraemon::renderer::RT64Context::update_config(
    const ultramodern::renderer::GraphicsConfig& old_config,
    const ultramodern::renderer::GraphicsConfig& new_config)
{
    if (!app || old_config == new_config) {
        return false;
    }
#if !defined(__ANDROID__)
    if ((old_config.wm_option != new_config.wm_option) ||
        (old_config.display_index != new_config.display_index) ||
        (old_config.display_width != new_config.display_width) ||
        (old_config.display_height != new_config.display_height) ||
        (old_config.display_refresh_rate != new_config.display_refresh_rate)) {
        app->setDisplayConfig(
            to_rt64(new_config.wm_option),
            new_config.display_index,
            new_config.display_width,
            new_config.display_height,
            new_config.display_refresh_rate);
    }
#endif
    apply_user_config(*app, new_config);
    app->updateUserConfig(true);
    if (old_config.msaa_option != new_config.msaa_option) {
        app->updateMultisampling();
    }
    return true;
}

void doraemon::renderer::RT64Context::enable_instant_present() {
    if (!app) {
        return;
    }
    app->enhancementConfig.presentation.mode =
        RT64::EnhancementConfiguration::Presentation::Mode::SkipBuffering;
    app->updateEnhancementConfig();
}

void doraemon::renderer::RT64Context::send_dl(const OSTask* task) {
    doraemon::cpu_probe::register_display_list();
    if (!app) {
        return;
    }
#if defined(__ANDROID__)
    static std::atomic_bool first_display_list_before{false};
    if (!first_display_list_before.exchange(true)) android_trace("first display list begin");
#endif
    {
        const auto& shared = app->sharedQueueResources;
        std::scoped_lock lock(shared->configurationMutex);
        double aspect = 4.0 / 3.0;
        if (shared->userConfig.aspectRatio == RT64::UserConfiguration::AspectRatio::Expand && shared->swapChainHeight > 0) {
            aspect = double(shared->swapChainWidth) / shared->swapChainHeight;
        }
        else if (shared->userConfig.aspectRatio == RT64::UserConfiguration::AspectRatio::Manual) {
            aspect = shared->userConfig.aspectTarget;
        }
        doraemon::sky::set_output_width(float(aspect * 240.0));
    }
    app->state->rsp->reset();
    doraemon::lighting::begin_task(task->t.data_ptr, task->t.data_size);
    const auto frameDisplayList = doraemon::lighting::current_display_list();
    const auto frameAssets = doraemon::lighting::current_frame_assets();
    const auto skyDisplayLists = doraemon::lighting::current_sky_display_lists();
#if defined(__ANDROID__) && defined(DORA64_ANDROID_DIAGNOSTICS)
    static bool reportedSkySnapshot = false;
    static bool reportedSkyReuse = false;
    if (skyDisplayLists.data != nullptr) {
        if (!reportedSkySnapshot) {
            std::fprintf(stderr, "Dora64 sky DL snapshot active: begin=%08X bytes=%u\n",
                skyDisplayLists.begin, skyDisplayLists.end - skyDisplayLists.begin);
            std::fflush(stderr);
            reportedSkySnapshot = true;
        }
        if (!reportedSkyReuse) {
            const auto* liveSky = app->core.RDRAM + skyDisplayLists.begin;
            const std::size_t skyLength = skyDisplayLists.end - skyDisplayLists.begin;
            if (std::memcmp(skyDisplayLists.data, liveSky, skyLength) != 0) {
                std::size_t offset = 0;
                while (skyDisplayLists.data[offset] == liveSky[offset]) ++offset;
                std::fprintf(stderr,
                    "Dora64 sky DL changed before parse: address=%08X captured=%02X live=%02X\n",
                    skyDisplayLists.begin + static_cast<std::uint32_t>(offset),
                    static_cast<unsigned>(skyDisplayLists.data[offset]),
                    static_cast<unsigned>(liveSky[offset]));
                std::fflush(stderr);
                reportedSkyReuse = true;
            }
        }
    }
    // A single report proves that the game reused the bank while RT64 still
    // had an older task pending. Parsing uses the frame's immutable copy.
    static bool reportedAssetReuse = false;
    if (!reportedAssetReuse && frameAssets.data != nullptr) {
        const auto* live = app->core.RDRAM + frameAssets.begin;
        const std::size_t length = frameAssets.end - frameAssets.begin;
        if (std::memcmp(frameAssets.data, live, length) != 0) {
            std::size_t offset = 0;
            while (frameAssets.data[offset] == live[offset]) ++offset;
            std::fprintf(stderr,
                "Dora64 frame assets changed before parse: address=%08X captured=%02X live=%02X\n",
                frameAssets.begin + static_cast<std::uint32_t>(offset),
                static_cast<unsigned>(frameAssets.data[offset]), static_cast<unsigned>(live[offset]));
            std::fflush(stderr);
            reportedAssetReuse = true;
        }
    }
    auto compare_frame_display_list = [&](const char* phase) {
        if (frameDisplayList.data == nullptr) return;
        const auto* live = app->core.RDRAM + frameDisplayList.begin;
        std::size_t changed = 0;
        std::size_t first = 0;
        const auto size = frameDisplayList.end - frameDisplayList.begin;
        for (std::size_t i = 0; i < size; ++i) {
            if (live[i] != frameDisplayList.data[i]) {
                if (changed == 0) first = i;
                ++changed;
            }
        }
        if (changed != 0) {
            static std::uint32_t reports = 0;
            if (reports++ < 32 || (reports % 256) == 0) {
                std::fprintf(stderr,
                    "Dora64 primary DL changed %s: task=%08X bytes=%zu first=%08X report=%u\n",
                    phase, task->t.data_ptr & 0x1FFFFFFFU, changed,
                    frameDisplayList.begin + static_cast<std::uint32_t>(first), reports);
                std::fflush(stderr);
            }
        }
    };
    compare_frame_display_list("before parse");
#endif
    app->state->displayListSnapshot = frameDisplayList.data;
    app->state->displayListSnapshotBegin = frameDisplayList.begin;
    app->state->displayListSnapshotEnd = frameDisplayList.end;
    app->state->frameAssetSnapshot = frameAssets.data;
    app->state->frameAssetSnapshotBegin = frameAssets.begin;
    app->state->frameAssetSnapshotEnd = frameAssets.end;
    app->state->skyDisplayListSnapshot = skyDisplayLists.data;
    app->state->skyDisplayListSnapshotBegin = skyDisplayLists.begin;
    app->state->skyDisplayListSnapshotEnd = skyDisplayLists.end;
    app->state->matrixDataCallback = doraemon::lighting::model_matrix_data;
    app->state->rsp->lightDataCallback = doraemon::lighting::light_data;
    app->interpreter->loadUCodeGBI(
        task->t.ucode & 0x3FFFFFF,
        task->t.ucode_data & 0x3FFFFFF,
        true);
    if (auto* gbi = app->interpreter->hleGBI) {
        if (gbi->map[0x01] == &RT64::GBI_F3D::matrix) {
            gbi->map[0x01] = &model_pool_matrix;
        }
    }
    app->processDisplayLists(
        app->core.RDRAM,
        task->t.data_ptr & 0x3FFFFFF,
        0,
        true);
#if defined(__ANDROID__) && defined(DORA64_ANDROID_DIAGNOSTICS)
    compare_frame_display_list("after parse");
#endif
#if defined(__ANDROID__)
    static std::atomic_bool first_display_list_after{false};
    if (!first_display_list_after.exchange(true)) android_trace("first display list complete");
#endif
    app->state->displayListSnapshot = nullptr;
    app->state->displayListSnapshotBegin = 0;
    app->state->displayListSnapshotEnd = 0;
    app->state->frameAssetSnapshot = nullptr;
    app->state->frameAssetSnapshotBegin = 0;
    app->state->frameAssetSnapshotEnd = 0;
    app->state->skyDisplayListSnapshot = nullptr;
    app->state->skyDisplayListSnapshotBegin = 0;
    app->state->skyDisplayListSnapshotEnd = 0;
    app->state->matrixDataCallback = nullptr;
    doraemon::lighting::end_task();
}

void doraemon::renderer::RT64Context::send_dummy_workload(uint32_t) {
    // Used only by the runtime before the game starts; update_screen presents the VI.
}

void doraemon::renderer::RT64Context::update_screen(bool cpu_changes_only) {
    if (app) {
#if defined(__ANDROID__)
        static std::atomic_bool first_screen_before{false};
        if (!first_screen_before.exchange(true)) android_trace("first screen update begin");
#endif
        sync_localized_textures();
        if (cpu_changes_only) {
            app->updateScreenIfFramebufferChanged();
        }
        else {
            app->updateScreen();
        }
#if defined(__ANDROID__)
        static std::atomic_bool first_screen_after{false};
        if (!first_screen_after.exchange(true)) android_trace("first screen update complete");
#endif
    }
}

void doraemon::renderer::RT64Context::sync_localized_textures() {
    if (!app || !app->textureCache) {
        return;
    }

    const auto& language = doraemon::localization::current_language();
    if (texture_language_initialized &&
        language.code == loaded_texture_language) {
        return;
    }

    texture_language_initialized = true;
    loaded_texture_language = language.code;
    if (language.original) {
        app->textureCache->clearReplacementDirectories();
        std::printf("Localized RT64 textures disabled (original Japanese)\n");
        return;
    }

    const std::filesystem::path replacement_directory =
        doraemon::localization::texture_directory(language);
    if (!std::filesystem::is_directory(replacement_directory)) {
        app->textureCache->clearReplacementDirectories();
        std::fprintf(
            stderr,
            "Localized RT64 texture directory is missing: %s\n",
            replacement_directory.string().c_str());
        return;
    }

    const bool loaded = app->textureCache->loadReplacementDirectory(
        RT64::ReplacementDirectory(replacement_directory));
    if (loaded) {
        std::printf(
            "Localized RT64 textures loaded: %s\n",
            replacement_directory.string().c_str());
    }
    else {
        std::fprintf(
            stderr,
            "Unable to load localized RT64 textures: %s\n",
            replacement_directory.string().c_str());
    }
}

void doraemon::renderer::RT64Context::reset_game() {
    doraemon::lighting::reset();
    doraemon::sky::reset();
    doraemon_model_pool_reset();
    doraemon::camera::reset();
    doraemon_cheats_reset();
    if (app) {
        app->state->resetGameSession();
    }
}

void doraemon::renderer::RT64Context::shutdown() {
    if (app) {
        app->end();
    }
}

uint32_t doraemon::renderer::RT64Context::get_display_framerate() const {
    if (!app || !app->presentQueue) {
        return 60;
    }
    return app->presentQueue->ext.sharedResources->swapChainRate;
}

float doraemon::renderer::RT64Context::get_resolution_scale() const {
    if (!app) {
        return 1.0f;
    }
    constexpr int reference_height = 240;
    switch (app->userConfig.resolution) {
        case RT64::UserConfiguration::Resolution::WindowIntegerScale:
            if (app->sharedQueueResources->swapChainHeight > 0) {
                return std::max(
                    float((app->sharedQueueResources->swapChainHeight +
                        reference_height - 1) / reference_height),
                    1.0f);
            }
            return 1.0f;
        case RT64::UserConfiguration::Resolution::Manual:
            return float(app->userConfig.resolutionMultiplier);
        case RT64::UserConfiguration::Resolution::Original:
        case RT64::UserConfiguration::Resolution::OptionCount:
            return 1.0f;
    }
    return 1.0f;
}

std::unique_ptr<ultramodern::renderer::RendererContext>
doraemon::renderer::create_render_context(
    uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode)
{
    return std::make_unique<RT64Context>(rdram, window_handle, developer_mode);
}
