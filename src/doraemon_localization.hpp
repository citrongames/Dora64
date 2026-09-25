#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace doraemon::localization {
    struct LanguageInfo {
        std::string code;
        std::string name;
        std::string font;
        bool original = false;
    };

    void initialize();
    const std::vector<LanguageInfo>& available_languages();
    std::size_t find_language(std::string_view code);
    void set_language(std::size_t language);
    std::size_t language();
    const LanguageInfo& current_language();
    // Resolve textures beside the manifest used for dialogue and fonts.
    std::filesystem::path texture_directory(const LanguageInfo& language);
}

// Called from the recompiled dialogue parser before it consumes a message.
// The object address is an N64 virtual address in RDRAM.
extern "C" void doraemon_localize_dialogue_message(
    std::uint8_t* rdram,
    std::uint32_t dialogue_object_address
);

// Returns zero for the original renderer spacing, or the localized glyph
// advance in pixels for an object currently using a translated message.
extern "C" int doraemon_dialogue_glyph_advance(
    std::uint8_t* rdram,
    std::uint32_t dialogue_object_address
);

// Hooks used by the game's item/time message composers. Category 1 is an
// item/name fragment terminated by FE; category 2 is a notification fragment
// terminated by FF. Prefix mode 1 retains a leading service byte that is part
// of the source pointer; mode 2 restores the byte immediately before an
// already-advanced source pointer.
extern "C" void doraemon_begin_game_text_composition(std::uint8_t* rdram);
extern "C" std::uint32_t doraemon_localize_game_text_fragment(
    std::uint8_t* rdram,
    std::uint32_t source_address,
    int category,
    int prefix_mode
);
// The result screen always uses the first two item-name records for its
// current/best-time labels. Addressing them explicitly avoids depending on a
// transient source-table pointer during unusual finish-state transitions.
extern "C" std::uint32_t doraemon_localize_time_label(
    std::uint8_t* rdram,
    std::uint32_t source_address,
    int label_index
);
extern "C" std::uint32_t doraemon_localize_game_text_digit(
    std::uint32_t original_code
);
extern "C" void doraemon_finish_game_text_composition(std::uint8_t* rdram);
