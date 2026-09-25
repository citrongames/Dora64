#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>
#include <fstream>

#include "librecomp/game.hpp"
#include "xxHash/xxh3.h"
#include "librecomp/rsp.hpp"
#include "ultramodern/config.hpp"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/ultramodern.hpp"

#include "rt64_render_context.hpp"
#include "doraemon_audio.hpp"
#include "doraemon_input.hpp"
#include "doraemon_lighting.hpp"
#include "system_overlay.hpp"
#include "funcs.h"

#include <SDL.h>
#if defined(__ANDROID__)
#include <SDL_system.h>
#endif
#include "stb/stb_image.h"
#if !defined(__ANDROID__)
#include "doraemon_icon.h"
#endif
#if defined(_WIN32)
#include <SDL_syswm.h>
#endif

void register_doraemon_overlays();

extern RspExitReason aspMain(uint8_t* rdram, uint32_t ucode_addr);

static RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    if (task->t.type == M_AUDTASK) {
        return aspMain;
    }

    std::fprintf(stderr, "RSP task type %u is not handled by RSPRecomp\n",
                 static_cast<unsigned>(task->t.type));

    return nullptr;
}

static bool is_supported_rom(const std::filesystem::path& path, uint64_t expected_hash) {
    // The supported Japanese dump is exactly 8 MiB. Avoid loading unrelated large files.
    constexpr size_t rom_size = 8 * 1024 * 1024;
    std::error_code error;
    if (std::filesystem::file_size(path, error) != rom_size || error) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data(rom_size);
    if (!file.read(reinterpret_cast<char*>(data.data()), data.size())) {
        return false;
    }
    return XXH3_64bits(data.data(), data.size()) == expected_hash;
}

static void show_error_message(const char* message) {
    // SDL message boxes are modal and may be shown before initializing the game window.
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dora64", message, nullptr) != 0) {
        std::fprintf(stderr, "Could not display error dialog: %s\n%s\n", SDL_GetError(), message);
    }
}

static void* create_gfx() {
    return nullptr;
}

static void set_window_icon(SDL_Window* window) {
#if !defined(__ANDROID__)
    int width = 0, height = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(doraemon_icon_png),
        static_cast<int>(doraemon_icon_png_size), &width, &height, nullptr, 4);
    if (pixels == nullptr) {
        std::fprintf(stderr, "Could not decode the application icon: %s\n", stbi_failure_reason());
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    if (surface != nullptr) {
        SDL_SetWindowIcon(window, surface);
        SDL_FreeSurface(surface);
    }
    else {
        std::fprintf(stderr, "Could not create the application icon: %s\n", SDL_GetError());
    }
    stbi_image_free(pixels);
#else
    (void)window;
#endif
}

static ultramodern::renderer::WindowHandle create_window(void*) {
    SDL_SetHint(SDL_HINT_APP_NAME, "Dora64");
#if defined(__ANDROID__)
    // SDL otherwise replaces the manifest's landscape lock with FULL_USER
    // when creating a resizable Android window.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft");
#endif
#if defined(__linux__)
    // SDL2 reads these before video initialization. Match the desktop file ID.
    SDL_setenv("SDL_VIDEO_X11_WMCLASS", "dora64", 0);
    SDL_setenv("SDL_VIDEO_WAYLAND_WMCLASS", "dora64", 0);
    // WSL gamepad passthrough exposes the classic /dev/input/js* interface,
    // while SDL otherwise prefers /dev/input/event* and detects no devices.
    // Keep the default event interface on regular Linux installations.
    if ((std::getenv("WSL_INTEROP") != nullptr) ||
        (std::getenv("WSL_DISTRO_NAME") != nullptr)) {
        SDL_SetHint(SDL_HINT_LINUX_JOYSTICK_CLASSIC, "1");
    }
#endif

    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
#if defined(_WIN32)
        return {};
#else
        return nullptr;
#endif
    }

    doraemon::input::initialize_sdl();

    uint32_t window_flags = SDL_WINDOW_RESIZABLE;
#if defined(__ANDROID__)
    window_flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
#if defined(__linux__) || defined(__ANDROID__)
    window_flags |= SDL_WINDOW_VULKAN;
#endif

    SDL_Window* window = SDL_CreateWindow(
        "Dora64",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        window_flags
    );

    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
#if defined(_WIN32)
        return {};
#else
        return nullptr;
#endif
    }

    set_window_icon(window);

#if defined(_WIN32)
    SDL_SysWMinfo window_info{};
    SDL_VERSION(&window_info.version);
    if (SDL_GetWindowWMInfo(window, &window_info) != SDL_TRUE) {
        std::fprintf(stderr, "SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        return {};
    }
    return ultramodern::renderer::WindowHandle{
        window_info.info.win.window,
        GetCurrentThreadId()
    };
#else
    return window;
#endif
}

static void update_gfx(void*) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        doraemon::input::process_event(event);
        if (event.type == SDL_QUIT) {
            ultramodern::quit();
        }
    }
    doraemon::input::update();
}

static void start_doraemon_on_first_vi() {
    static std::once_flag start_once;
    std::call_once(start_once, []() {
        std::puts("Starting Dora64...");
        recomp::start_game(u8"doraemon.n64.jp", "");
    });
}

static std::filesystem::path executable_directory() {
    // Use the actual executable location, never the launcher's working directory.
    char* base_path = SDL_GetBasePath();
    if (base_path != nullptr) {
        const auto path = std::filesystem::u8path(base_path).lexically_normal();
        SDL_free(base_path);
        return path.has_filename() ? path : path.parent_path();
    }
    return {};
}

#if defined(__ANDROID__)
static std::filesystem::path android_data_directory() {
    const char* storage = SDL_AndroidGetInternalStoragePath();
    return storage != nullptr ? std::filesystem::u8path(storage) : std::filesystem::path{};
}

static std::filesystem::path android_user_directory() {
    if ((SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) == 0) {
        return {};
    }
    const char* storage = SDL_AndroidGetExternalStoragePath();
    if (storage == nullptr) return {};
    const auto path = std::filesystem::u8path(storage);
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return error ? std::filesystem::path{} : path;
}

static bool migrate_android_user_file(
    const std::filesystem::path& private_dir,
    const std::filesystem::path& user_dir,
    const std::filesystem::path& relative)
{
    const auto source = private_dir / relative;
    const auto destination = user_dir / relative;
    std::error_code error;
    if (!std::filesystem::exists(source, error)) return !error;
    if (std::filesystem::exists(destination, error)) return !error;
    if (error) return false;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return false;
    auto temporary = destination;
    temporary += ".migration-part";
    const bool copied = std::filesystem::copy_file(
        source, temporary, std::filesystem::copy_options::overwrite_existing, error);
    if (copied && !error) {
        std::filesystem::rename(temporary, destination, error);
    }
    if (!copied || error) {
        std::fprintf(stderr, "Could not migrate %s: %s\n",
            relative.string().c_str(), error.message().c_str());
        return false;
    }
    std::fprintf(stderr, "Migrated %s to Android/data\n", relative.string().c_str());
    return true;
}

static bool migrate_android_user_files(
    const std::filesystem::path& private_dir,
    const std::filesystem::path& user_dir)
{
    for (const auto* name : {
            "doraemon_pc_settings.json",
            "doraemon_input_settings.json",
            "saves/doraemon.n64.jp.bin",
            "saves/doraemon.n64.jp.bin.bak"}) {
        if (!migrate_android_user_file(private_dir, user_dir, name)) return false;
    }
    return true;
}

static std::filesystem::path wait_for_android_rom(
    const std::filesystem::path& data_dir, uint64_t expected_hash)
{
    const auto rom = data_dir / "doraemon.n64.jp.z64";
    const auto ready = data_dir / ".assets-ready";
    const auto canceled = data_dir / ".rom-selection-canceled";
    while (true) {
        std::error_code error;
        if (std::filesystem::exists(canceled, error)) return {};
        error.clear();
        if (std::filesystem::exists(ready, error) &&
            std::filesystem::is_regular_file(rom, error)) {
            if (is_supported_rom(rom, expected_hash)) return rom;
            std::filesystem::remove(rom, error);
            show_error_message("The selected ROM is not the supported original Japanese version. Restart Dora64 to choose another file.");
            return {};
        }
        SDL_Delay(100);
    }
}
#endif

static std::filesystem::path find_rom_path(
    const std::filesystem::path& executable_dir, uint64_t expected_hash)
{
    if (executable_dir.empty()) return {};
    std::vector<std::filesystem::path> candidates;
    std::error_code error;
    std::filesystem::directory_iterator it(
        executable_dir, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    while (!error && it != end) {
        const auto extension = it->path().extension().u8string();
        std::error_code file_error;
        if ((extension == u8".z64" || extension == u8".Z64") && it->is_regular_file(file_error)) {
            candidates.push_back(it->path());
        }
        it.increment(error);
    }
    std::sort(candidates.begin(), candidates.end());
    for (const auto& candidate : candidates) {
        if (is_supported_rom(candidate, expected_hash)) {
            return candidate;
        }
    }
    return {};
}

static bool has_command_line_argument(
    int argc,
    char** argv,
    std::string_view expected)
{
    for (int index = 1; index < argc; index++) {
        if ((argv[index] != nullptr) && (std::string_view(argv[index]) == expected)) {
            return true;
        }
    }

    return false;
}

#if defined(__ANDROID__)
#define main SDL_main
#endif
int main(int argc, char** argv) {
    std::puts("Dora64 - first runtime boot");

    register_doraemon_overlays();

    recomp::GameEntry game {
        .rom_hash = 0x58DA3ED217BDB534,
        .internal_name = "DORAEMON",
        .display_name = "Dora64",
        .game_id = u8"doraemon.n64.jp",
        .mod_game_id = "",
        .save_type = recomp::SaveType::AllowAll,
        .is_enabled = true,
        .has_compressed_code = false,
        .entrypoint_address = static_cast<gpr>(static_cast<int32_t>(0x80000400)),
        .entrypoint = recomp_entrypoint,
    };

#if defined(__ANDROID__)
    const auto executable_dir = android_data_directory();
    const auto user_dir = android_user_directory();
#else
    const auto executable_dir = executable_directory();
#endif
    if (executable_dir.empty()) {
        show_error_message("Could not determine the game folder. Please restart Dora64 from its installation folder.");
        SDL_Quit();
        return EXIT_FAILURE;
    }
#if defined(__ANDROID__)
    if (user_dir.empty()) {
        show_error_message("Android user storage is unavailable. Dora64 cannot safely load saves and settings.");
        SDL_Quit();
        return EXIT_FAILURE;
    }
    const auto native_log = user_dir / "native-stderr.log";
    if (std::freopen(native_log.c_str(), "w", stderr) != nullptr) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
#endif
#if defined(__ANDROID__)
    game.rom_path = wait_for_android_rom(executable_dir, game.rom_hash);
#else
    game.rom_path = find_rom_path(executable_dir, game.rom_hash);
#endif
    if (game.rom_path.empty()) {
#if defined(__ANDROID__)
        SDL_Quit();
        return EXIT_FAILURE;
#else
        const auto directory_utf8 = executable_dir.u8string();
        const std::string message =
            "No supported ROM found.\n\n"
            "Place the original Japanese ROM of Doraemon: Nobita to 3tsu no Seireiseki "
            "(.z64) in the same folder as Dora64.\n\n"
            "The filename can be anything. The file must match the supported original version.\n\n"
            "Game folder:\n" + std::string(directory_utf8.begin(), directory_utf8.end());
        std::fprintf(stderr, "%s\n", message.c_str());
        show_error_message(message.c_str());
        SDL_Quit();
        return EXIT_FAILURE;
#endif
    }

#if defined(__ANDROID__)
    if (!migrate_android_user_files(executable_dir, user_dir)) {
        show_error_message("Could not move existing saves and settings to Android/data. No data was removed; check free space and restart Dora64.");
        SDL_Quit();
        return EXIT_FAILURE;
    }
    const auto config_path = user_dir;
#else
    const auto config_path = game.rom_path.parent_path();
#endif
    const auto config_path_utf8 = config_path.u8string();
    std::printf("Config path: %s\n", reinterpret_cast<const char*>(config_path_utf8.c_str()));
    recomp::register_config_path(config_path);
    doraemon::system_overlay::initialize(config_path);
    doraemon::input::initialize(config_path);

    if (has_command_line_argument(argc, argv, "--rt64-developer")) {
        auto graphics_config = ultramodern::renderer::get_graphics_config();
        graphics_config.developer_mode = true;
        ultramodern::renderer::set_graphics_config(graphics_config);
        std::puts("RT64 developer mode enabled; press F1 to toggle the inspector.");
    }

    recomp::register_game(game);

    if (!doraemon::audio::initialize()) {
        std::fprintf(stderr, "Audio output is unavailable; continuing without sound.\n");
    }


    recomp::Configuration config {
    .project_version = recomp::Version{0, 0, 1},

    .window_handle = nullptr,

    .rsp_callbacks = {
        .get_rsp_microcode = get_rsp_microcode,
    },

    .renderer_callbacks = {
        .create_render_context = doraemon::renderer::create_render_context,
    },

    .audio_callbacks = {
        .queue_samples = doraemon::audio::queue_samples,
        .get_frames_remaining = doraemon::audio::get_frames_remaining,
        .set_frequency = doraemon::audio::set_frequency,
    },

    .input_callbacks = {
        .poll_input = doraemon::input::poll,
        .get_input = doraemon::input::get_input,
        .set_rumble = doraemon::input::set_rumble,
        .get_connected_device_info = doraemon::input::get_connected_device_info,
    },

    .gfx_callbacks = {
        .create_gfx = create_gfx,
        .create_window = create_window,
        .update_gfx = update_gfx,
    },

    .events_callbacks = {
        .vi_callback = start_doraemon_on_first_vi,
        .gfx_init_callback = nullptr,
        .gfx_task_submitted_callback = doraemon::lighting::submit_task,
    },

    .error_handling_callbacks = {
        .message_box = show_error_message,
    },
};

	recomp::start(config);
    doraemon::input::shutdown();
    doraemon::audio::shutdown();
    SDL_Quit();

    return 0;
}
