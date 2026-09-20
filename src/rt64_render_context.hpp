#pragma once

#include <memory>
#include <string>

#include "ultramodern/renderer_context.hpp"

namespace RT64 {
    struct Application;
}

namespace doraemon::renderer {
    class RT64Context final : public ultramodern::renderer::RendererContext {
    public:
        RT64Context(
            uint8_t* rdram,
            ultramodern::renderer::WindowHandle window_handle,
            bool developer_mode);
        ~RT64Context() override;

        bool valid() override;
        bool update_config(
            const ultramodern::renderer::GraphicsConfig& old_config,
            const ultramodern::renderer::GraphicsConfig& new_config) override;
        void enable_instant_present() override;
        void send_dl(const OSTask* task) override;
        void send_dummy_workload(uint32_t fb_address) override;
        void update_screen(bool cpu_changes_only = false) override;
        void reset_game() override;
        void shutdown() override;
        uint32_t get_display_framerate() const override;
        float get_resolution_scale() const override;

    private:
        void sync_localized_textures();

        std::unique_ptr<RT64::Application> app;
        std::string loaded_texture_language;
        bool texture_language_initialized = false;
    };

    std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(
        uint8_t* rdram,
        ultramodern::renderer::WindowHandle window_handle,
        bool developer_mode);
}
