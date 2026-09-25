#include "doraemon_localization.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SDL.h>

#include "json/json.hpp"
#include "librecomp/addresses.hpp"
#include "librecomp/game.hpp"

namespace {
    using doraemon::localization::LanguageInfo;

    constexpr std::uint32_t kN64Kseg0Base = 0x80000000U;
    constexpr std::uint32_t kN64Kseg0End = 0xA0000000U;
    constexpr std::uint32_t kMessagePointerOffset = 0x04;
    constexpr std::uint32_t kFontPointerOffset = 0x08;
    constexpr std::uint32_t kComposedMessagePointerTable = 0x801C7F00U;
    constexpr std::uint32_t kMessageScratchSize = 4096;
    constexpr std::uint32_t kScratchSlotSize = 1024;
    constexpr std::uint32_t kFirstComposedMessageOffset = 1024;
    constexpr std::uint32_t kSecondComposedMessageOffset = 2048;
    // 0x801C7F00/04 serve the two-message descriptor. 0x801C7F08 is
    // a separate alias of the first buffer used by single-message pickups.
    constexpr std::array<std::uint32_t, 3> kComposedMessageOffsets{
        kFirstComposedMessageOffset,
        kSecondComposedMessageOffset,
        kFirstComposedMessageOffset,
    };
    static_assert(
        kSecondComposedMessageOffset + kScratchSlotSize <= kMessageScratchSize
    );
    constexpr std::uint32_t kAtlasWidth = 192;
    constexpr std::uint32_t kAtlasHeight = 192;
    constexpr std::uint32_t kAtlasSize = kAtlasWidth * kAtlasHeight;
    constexpr std::uint32_t kGlyphSize = 12;
    constexpr std::size_t kDialogueGlyphSlotCount = 80;
    constexpr std::array<std::array<int, 3>, 6> kAtlasPaletteColors{{
        {{0, 0, 0}},
        {{255, 255, 255}},
        {{128, 128, 128}},
        {{255, 0, 0}},
        {{255, 128, 0}},
        {{255, 220, 0}},
    }};

    constexpr std::uint8_t kControlF7 = 0xF7;
    constexpr std::uint8_t kControlF8 = 0xF8;
    constexpr std::uint8_t kNoPortrait = 0x00;
    constexpr std::uint8_t kNewLine = 0xFE;
    constexpr std::uint8_t kMessageEnd = 0xFF;

    struct FontConfig {
        std::filesystem::path configPath;
        std::filesystem::path atlasPath;
        int glyphWidth = 0;
        int glyphSpacing = 0;
        std::size_t maximumLineLength = 0;
        std::size_t maximumLineCount = 0;
        std::size_t maximumVisibleCharacterCount = 0;
        std::unordered_map<char32_t, std::uint8_t> glyphs;
        std::array<bool, 256> cursorCodes{};
    };

    struct TranslationEntry {
        std::vector<std::uint8_t> source;
        std::vector<std::uint8_t> replacement;
    };

    struct LanguagePack {
        bool loadAttempted = false;
        bool loaded = false;
        bool itemCompositionComplete = false;
        FontConfig font;
        std::vector<TranslationEntry> entries;
        std::vector<TranslationEntry> itemDescriptions;
        std::vector<TranslationEntry> itemNames;
        std::vector<TranslationEntry> itemNotifications;
    };

    struct LocalizationState {
        std::uint32_t messageAddress = 0;
        std::uint32_t atlasAddress = 0;
        std::uint32_t originalAtlasAddress = 0;
        bool atlasReady = false;
        bool allocationFailureReported = false;
        int activeGlyphAdvance = 0;
        std::filesystem::path loadedFontConfigPath;
    };

    std::atomic<std::size_t> selectedLanguage = 0;
    std::once_flag languageInitialization;
    std::vector<LanguageInfo> languages;
    std::vector<LanguagePack> languagePacks;
    std::filesystem::path localizationRoot;
    LocalizationState state;
    bool composedMessageUsesLocalizedFont = false;
    std::unordered_set<std::uint32_t> localizedComposedMessages;
    std::unordered_set<std::uint32_t> pendingComposedMessages;
    std::array<std::uint32_t, kComposedMessageOffsets.size()> originalComposedMessagePointers{};
    bool composedMessagePointersRedirected = false;

    std::uint8_t readByte(const std::uint8_t* rdram, std::uint32_t n64Address) {
        return rdram[((n64Address - kN64Kseg0Base) ^ 3U)];
    }

    void writeByte(
        std::uint8_t* rdram,
        std::uint32_t n64Address,
        std::uint8_t value
    ) {
        rdram[((n64Address - kN64Kseg0Base) ^ 3U)] = value;
    }

    std::uint32_t readWord(const std::uint8_t* rdram, std::uint32_t n64Address) {
        std::uint32_t value = 0;
        std::memcpy(
            &value,
            rdram + (n64Address - kN64Kseg0Base),
            sizeof(value)
        );
        return value;
    }

    void writeWord(
        std::uint8_t* rdram,
        std::uint32_t n64Address,
        std::uint32_t value
    ) {
        std::memcpy(
            rdram + (n64Address - kN64Kseg0Base),
            &value,
            sizeof(value)
        );
    }

    void restoreComposedMessagePointers(std::uint8_t* rdram) {
        if (rdram == nullptr || !composedMessagePointersRedirected) {
            return;
        }
        for (std::uint32_t index = 0; index < kComposedMessageOffsets.size(); index++) {
            writeWord(
                rdram,
                kComposedMessagePointerTable + index * 4U,
                originalComposedMessagePointers[index]
            );
        }
        composedMessagePointersRedirected = false;
    }

    bool isRdramAddress(std::uint32_t n64Address) {
        return n64Address >= kN64Kseg0Base && n64Address < kN64Kseg0End;
    }

    std::filesystem::path pathFromUtf8(const char* text) {
#if defined(_WIN32)
        return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(text)
        ));
#else
        return std::filesystem::path(text);
#endif
    }

    std::string pathToUtf8(const std::filesystem::path& path) {
#if defined(_WIN32)
        const std::u8string utf8 = path.u8string();
        return std::string(
            reinterpret_cast<const char*>(utf8.data()),
            utf8.size()
        );
#else
        return path.string();
#endif
    }

    std::optional<std::filesystem::path> findAsset(
        const std::filesystem::path& relativePath
    ) {
        std::vector<std::filesystem::path> candidates;
#if defined(__ANDROID__)
        candidates.push_back(recomp::get_config_path() / "assets" / relativePath);
        // Bundled assets remain private; only saves and settings move to the
        // user-visible Android/data directory.
        if (const char* internal = SDL_AndroidGetInternalStoragePath()) {
            candidates.push_back(pathFromUtf8(internal) / "assets" / relativePath);
        }
#endif
        if (char* basePath = SDL_GetBasePath()) {
            candidates.push_back(pathFromUtf8(basePath) / "assets" / relativePath);
            SDL_free(basePath);
        }

        std::error_code error;
        const std::filesystem::path current = std::filesystem::current_path(error);
        if (!error) {
            candidates.push_back(current / "assets" / relativePath);
        }

        for (const std::filesystem::path& candidate : candidates) {
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                return std::filesystem::absolute(candidate).lexically_normal();
            }
            error.clear();
        }
        return std::nullopt;
    }

    bool loadJson(
        const std::filesystem::path& path,
        nlohmann::json& output,
        std::string& error
    ) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "unable to open " + pathToUtf8(path);
            return false;
        }

        try {
            input >> output;
        }
        catch (const std::exception& exception) {
            error = pathToUtf8(path) + ": " + exception.what();
            return false;
        }
        return true;
    }

    bool validLanguageCode(std::string_view code) {
        if (code.empty() || code.size() > 16) {
            return false;
        }
        for (const char character : code) {
            const bool asciiLetter =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z');
            const bool digit = character >= '0' && character <= '9';
            if (!asciiLetter && !digit && character != '-' && character != '_') {
                return false;
            }
        }
        return true;
    }

    bool validRelativeAssetPath(std::string_view value) {
        if (value.empty()) {
            return false;
        }
        const std::string utf8Value(value);
        const std::filesystem::path path = pathFromUtf8(utf8Value.c_str());
        if (path.is_absolute()) {
            return false;
        }
        for (const std::filesystem::path& component : path) {
            if (component == "..") {
                return false;
            }
        }
        return true;
    }

    void loadLanguageManifest() {
        LanguageInfo original{
            .code = "ja",
            .name = "Original Japanese",
            .font = "",
            .original = true,
        };
        languages = { original };

        const auto manifestPath = findAsset(
            std::filesystem::path("localization") / "languages.json");
        if (!manifestPath.has_value()) {
            languagePacks.clear();
            languagePacks.resize(languages.size());
            std::printf(
                "Localization manifest not found; original Japanese only\n");
            return;
        }

        localizationRoot = manifestPath->parent_path();
        nlohmann::json manifest;
        std::string error;
        if (!loadJson(*manifestPath, manifest, error)) {
            std::fprintf(stderr, "Unable to load localization manifest: %s\n", error.c_str());
            languagePacks.clear();
            languagePacks.resize(languages.size());
            return;
        }

        try {
            if (manifest.at("format_version").get<int>() != 1) {
                throw std::runtime_error("unsupported format_version");
            }
            const nlohmann::json& entries = manifest.at("languages");
            if (!entries.is_array()) {
                throw std::runtime_error("languages must be an array");
            }

            for (const nlohmann::json& entry : entries) {
                if (!entry.value("original", false)) {
                    continue;
                }
                const std::string code = entry.at("code").get<std::string>();
                const std::string name = entry.at("name").get<std::string>();
                if (!validLanguageCode(code) || name.empty()) {
                    throw std::runtime_error("invalid original language entry");
                }
                original.code = code;
                original.name = name;
                languages.front() = original;
                break;
            }

            std::unordered_set<std::string> knownCodes{ languages.front().code };
            for (const nlohmann::json& entry : entries) {
                if (entry.value("original", false)) {
                    continue;
                }

                const std::string code = entry.at("code").get<std::string>();
                const std::string name = entry.at("name").get<std::string>();
                const std::string font = entry.at("font").get<std::string>();
                if (!validLanguageCode(code) || name.empty() ||
                    !validRelativeAssetPath(font) || !knownCodes.insert(code).second) {
                    std::fprintf(
                        stderr,
                        "Skipping invalid or duplicate localization language: %s\n",
                        code.c_str());
                    continue;
                }

                languages.push_back(LanguageInfo{
                    .code = code,
                    .name = name,
                    .font = font,
                    .original = false,
                });
            }
        }
        catch (const std::exception& exception) {
            std::fprintf(
                stderr,
                "Unable to parse localization manifest %s: %s\n",
                pathToUtf8(*manifestPath).c_str(),
                exception.what());
            languages = { original };
        }

        selectedLanguage.store(0, std::memory_order_release);
        languagePacks.clear();
        languagePacks.resize(languages.size());
        std::printf(
            "Localization manifest loaded: %zu language(s)\n",
            languages.size());
    }

    void ensureLanguagesInitialized() {
        std::call_once(languageInitialization, loadLanguageManifest);
    }

    bool decodeUtf8(std::string_view input, std::u32string& output) {
        output.clear();
        for (std::size_t index = 0; index < input.size();) {
            const auto first = static_cast<unsigned char>(input[index]);
            char32_t codePoint = 0;
            std::size_t length = 0;
            char32_t minimum = 0;

            if (first < 0x80U) {
                codePoint = first;
                length = 1;
            }
            else if ((first & 0xE0U) == 0xC0U) {
                codePoint = first & 0x1FU;
                length = 2;
                minimum = 0x80;
            }
            else if ((first & 0xF0U) == 0xE0U) {
                codePoint = first & 0x0FU;
                length = 3;
                minimum = 0x800;
            }
            else if ((first & 0xF8U) == 0xF0U) {
                codePoint = first & 0x07U;
                length = 4;
                minimum = 0x10000;
            }
            else {
                return false;
            }

            if (index + length > input.size()) {
                return false;
            }
            for (std::size_t continuation = 1; continuation < length; continuation++) {
                const auto byte = static_cast<unsigned char>(input[index + continuation]);
                if ((byte & 0xC0U) != 0x80U) {
                    return false;
                }
                codePoint = (codePoint << 6) | (byte & 0x3FU);
            }

            if (codePoint < minimum || codePoint > 0x10FFFF ||
                (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
                return false;
            }
            output.push_back(codePoint);
            index += length;
        }
        return true;
    }

    bool readSourceBytes(
        const nlohmann::json& record,
        std::vector<std::uint8_t>& output,
        std::uint8_t expectedTerminator = kMessageEnd
    ) {
        output.clear();
        // The shipped index contains offsets and lengths, never original text.
        // The runtime has already validated and loaded the user's Japanese ROM.
        const auto rom = recomp::get_rom();
        const std::string id = record.at("id").get<std::string>();
        std::size_t offset = 0;
        const auto result = std::from_chars(id.data(), id.data() + id.size(), offset, 16);
        const auto& lengthJson = record.at("byte_length");
        if (id.size() != 8 || result.ec != std::errc{} ||
            result.ptr != id.data() + id.size() || !lengthJson.is_number_unsigned()) {
            return false;
        }
        const std::size_t length = lengthJson.get<std::size_t>();
        if (length < 2 || length > kMessageScratchSize || offset > rom.size() ||
            length > rom.size() - offset || rom[offset + length - 1] != expectedTerminator) {
            return false;
        }
        output.assign(rom.begin() + offset, rom.begin() + offset + length);
        return true;
    }

    int hexNibble(char32_t character) {
        if (character >= U'0' && character <= U'9') {
            return static_cast<int>(character - U'0');
        }
        if (character >= U'A' && character <= U'F') {
            return static_cast<int>(character - U'A') + 10;
        }
        if (character >= U'a' && character <= U'f') {
            return static_cast<int>(character - U'a') + 10;
        }
        return -1;
    }

    bool loadFontConfig(
        const std::filesystem::path& path,
        FontConfig& font,
        std::string& error
    ) {
        nlohmann::json json;
        if (!loadJson(path, json, error)) {
            return false;
        }

        try {
            if (json.at("format_version").get<int>() != 1 ||
                json.at("tile_width").get<int>() != static_cast<int>(kGlyphSize) ||
                json.at("tile_height").get<int>() != static_cast<int>(kGlyphSize)) {
                error = pathToUtf8(path) + ": unsupported font format or tile size";
                return false;
            }

            font = {};
            font.configPath = std::filesystem::absolute(path).lexically_normal();
            font.atlasPath = (font.configPath.parent_path() /
                pathFromUtf8(json.at("atlas").get_ref<const std::string&>().c_str()))
                .lexically_normal();
            font.glyphWidth = json.at("glyph_width").get<int>();
            font.glyphSpacing = json.at("glyph_spacing").get<int>();
            font.maximumLineLength = json.at("max_characters_per_line").get<std::size_t>();
            font.maximumLineCount = json.at("max_lines").get<std::size_t>();
            font.maximumVisibleCharacterCount =
                json.at("max_visible_characters_per_message").get<std::size_t>();
            const int glyphAdvance = font.glyphWidth + font.glyphSpacing;
            if (font.glyphWidth <= 0 || font.glyphWidth > static_cast<int>(kGlyphSize) ||
                font.glyphSpacing < 0 || font.glyphSpacing > static_cast<int>(kGlyphSize) ||
                glyphAdvance > static_cast<int>(kGlyphSize) ||
                font.maximumLineLength == 0 || font.maximumLineCount == 0 ||
                font.maximumVisibleCharacterCount == 0 ||
                font.maximumVisibleCharacterCount > kDialogueGlyphSlotCount) {
                error = pathToUtf8(path) + ": invalid font layout values";
                return false;
            }

            for (const nlohmann::json& cursorCode : json.at("cursor_codes")) {
                const int value = cursorCode.get<int>();
                if (value < 0 || value > 0xFF) {
                    error = pathToUtf8(path) + ": invalid cursor code";
                    return false;
                }
                font.cursorCodes[static_cast<std::size_t>(value)] = true;
            }

            const nlohmann::json& glyphs = json.at("glyphs");
            if (!glyphs.is_object()) {
                error = pathToUtf8(path) + ": glyphs must be an object";
                return false;
            }
            for (auto iterator = glyphs.begin(); iterator != glyphs.end(); ++iterator) {
                std::u32string decodedKey;
                if (!decodeUtf8(iterator.key(), decodedKey) || decodedKey.size() != 1) {
                    error = pathToUtf8(path) + ": every glyph key must be one character";
                    return false;
                }

                const int value = iterator.value().get<int>();
                if (value < 0 || value > 0xFF || value == kControlF7 ||
                    value == kControlF8 || value == kNewLine || value == kMessageEnd ||
                    font.cursorCodes[static_cast<std::size_t>(value)]) {
                    error = pathToUtf8(path) + ": glyph uses a reserved byte code";
                    return false;
                }
                font.glyphs.emplace(
                    decodedKey.front(),
                    static_cast<std::uint8_t>(value)
                );
            }
        }
        catch (const std::exception& exception) {
            error = pathToUtf8(path) + ": " + exception.what();
            return false;
        }

        return true;
    }

    bool appendEncodedText(
        const FontConfig& font,
        std::u32string_view text,
        std::vector<std::uint8_t>& encoded,
        std::string& error,
        bool enforceDialogueLimits,
        bool enforceVisibleLimit
    ) {
        std::size_t lineCount = 1;
        std::size_t lineLength = 0;
        std::size_t visibleCharacterCount = 0;
        for (std::size_t index = 0; index < text.size();) {
            const char32_t character = text[index];
            if (character == U'\r') {
                index++;
                continue;
            }
            if (character == U'\n') {
                if (enforceDialogueLimits &&
                    (lineLength > font.maximumLineLength ||
                     lineCount >= font.maximumLineCount)) {
                    error = "translation exceeds the configured dialogue box";
                    return false;
                }
                encoded.push_back(kNewLine);
                lineCount++;
                lineLength = 0;
                index++;
                continue;
            }

            if (index + 3 < text.size() && text.substr(index, 4) == U"{F7}") {
                encoded.push_back(kControlF7);
                index += 4;
                continue;
            }
            if (index + 6 < text.size() && text[index] == U'{' &&
                text[index + 1] == U'F' && text[index + 2] == U'8' &&
                text[index + 3] == U':' && text[index + 6] == U'}') {
                const int high = hexNibble(text[index + 4]);
                const int low = hexNibble(text[index + 5]);
                if (high < 0 || low < 0) {
                    error = "invalid {F8:XX} control";
                    return false;
                }
                encoded.push_back(kControlF8);
                encoded.push_back(static_cast<std::uint8_t>((high << 4) | low));
                index += 7;
                continue;
            }

            const auto glyph = font.glyphs.find(character);
            if (glyph == font.glyphs.end()) {
                error = "translation contains a character missing from the font";
                return false;
            }
            encoded.push_back(glyph->second);
            lineLength++;
            visibleCharacterCount++;
            if (enforceDialogueLimits && lineLength > font.maximumLineLength) {
                error = "translation exceeds the configured line length";
                return false;
            }
            if (enforceVisibleLimit &&
                visibleCharacterCount > font.maximumVisibleCharacterCount) {
                error = "translation exceeds the renderer's visible-character capacity";
                return false;
            }
            index++;
        }

        if (enforceDialogueLimits && lineLength + 1 > font.maximumLineLength) {
            error = "translation leaves no room for the dialogue cursor";
            return false;
        }
        return true;
    }

    bool encodeMessage(
        const FontConfig& font,
        std::uint8_t serviceByte,
        std::u32string_view text,
        std::vector<std::uint8_t>& encoded,
        std::string& error
    ) {
        encoded.clear();
        encoded.reserve(text.size() + 8);
        encoded.push_back(serviceByte);
        if (!appendEncodedText(font, text, encoded, error, true, true)) {
            return false;
        }
        encoded.push_back(kMessageEnd);
        return true;
    }

    bool encodeFragment(
        const FontConfig& font,
        std::u32string_view text,
        std::uint8_t terminator,
        std::vector<std::uint8_t>& encoded,
        std::string& error
    ) {
        encoded.clear();
        encoded.reserve(text.size() + 8);
        if (!appendEncodedText(font, text, encoded, error, false, true)) {
            return false;
        }
        encoded.push_back(terminator);
        return true;
    }

    bool loadLanguagePack(std::size_t language, LanguagePack& pack) {
        pack.loadAttempted = true;
        ensureLanguagesInitialized();
        if (language >= languages.size() || languages[language].original) {
            return false;
        }

        const LanguageInfo& info = languages[language];
        const std::filesystem::path translationPath =
            localizationRoot / info.code / "dialogue.json";
        std::error_code pathError;
        if (!std::filesystem::is_regular_file(translationPath, pathError) || pathError) {
            std::fprintf(
                stderr,
                "Unable to find dialogue localization: %s\n",
                pathToUtf8(translationPath).c_str());
            return false;
        }

        nlohmann::json translationJson;
        nlohmann::json sourceJson;
        nlohmann::json gameTranslationJson;
        nlohmann::json gameSourceJson;
        std::string error;
        const std::filesystem::path sourcePath = localizationRoot / "dialogue_index.json";
        const std::filesystem::path gameTranslationPath =
            localizationRoot / info.code / "game_text.json";
        const std::filesystem::path gameSourcePath = localizationRoot / "game_text_index.json";
        if (!loadJson(translationPath, translationJson, error) ||
            !loadJson(sourcePath, sourceJson, error) ||
            !loadJson(gameTranslationPath, gameTranslationJson, error) ||
            !loadJson(gameSourcePath, gameSourceJson, error)) {
            std::fprintf(stderr, "Unable to load text localization: %s\n", error.c_str());
            return false;
        }

        try {
            if (translationJson.at("format_version").get<int>() != 1 ||
                sourceJson.at("format_version").get<int>() != 1 ||
                gameTranslationJson.at("format_version").get<int>() != 1 ||
                gameSourceJson.at("format_version").get<int>() != 1) {
                std::fprintf(stderr, "Unsupported text localization format\n");
                return false;
            }
            if (translationJson.at("language").get<std::string>() != info.code ||
                gameTranslationJson.at("language").get<std::string>() != info.code) {
                std::fprintf(
                    stderr,
                    "Text localization language does not match folder %s\n",
                    info.code.c_str());
                return false;
            }

            const std::filesystem::path fontPath =
                (localizationRoot / pathFromUtf8(info.font.c_str()))
                .lexically_normal();
            if (!loadFontConfig(fontPath, pack.font, error)) {
                std::fprintf(stderr, "Unable to load dialogue font: %s\n", error.c_str());
                return false;
            }

            std::unordered_map<std::string, std::vector<std::uint8_t>> sources;
            for (const nlohmann::json& message : sourceJson.at("messages")) {
                const std::string id = message.at("id").get<std::string>();
                std::vector<std::uint8_t> bytes;
                if (!readSourceBytes(message, bytes)) {
                    std::fprintf(stderr, "Invalid source bytes for dialogue %s\n", id.c_str());
                    continue;
                }
                sources.emplace(id, std::move(bytes));
            }

            std::size_t translatedCount = 0;
            for (const nlohmann::json& message : translationJson.at("messages")) {
                const std::string id = message.at("id").get<std::string>();
                const std::string translation = message.at("translation").get<std::string>();
                if (translation.empty()) {
                    continue;
                }

                const auto source = sources.find(id);
                if (source == sources.end() || source->second.empty()) {
                    std::fprintf(stderr, "Dialogue %s has no source entry\n", id.c_str());
                    continue;
                }

                std::u32string decodedTranslation;
                if (!decodeUtf8(translation, decodedTranslation)) {
                    std::fprintf(stderr, "Dialogue %s is not valid UTF-8\n", id.c_str());
                    continue;
                }

                TranslationEntry entry;
                entry.source = source->second;
                if (!encodeMessage(
                        pack.font,
                        entry.source.front(),
                        decodedTranslation,
                        entry.replacement,
                        error
                    )) {
                    std::fprintf(
                        stderr,
                        "Skipping dialogue %s: %s\n",
                        id.c_str(),
                        error.c_str()
                    );
                    continue;
                }
                pack.entries.push_back(std::move(entry));
                translatedCount++;
            }

            struct GameSource {
                std::string category;
                std::vector<std::uint8_t> bytes;
            };
            std::unordered_map<std::string, GameSource> gameSources;
            std::size_t itemNameSourceCount = 0;
            std::size_t itemNotificationSourceCount = 0;
            for (const nlohmann::json& record : gameSourceJson.at("records")) {
                const std::string id = record.at("id").get<std::string>();
                GameSource source;
                source.category = record.at("category").get<std::string>();
                const std::uint8_t terminator = source.category == "item_name"
                    ? kNewLine
                    : kMessageEnd;
                if (!readSourceBytes(
                        record,
                        source.bytes,
                        terminator
                    )) {
                    std::fprintf(stderr, "Invalid source bytes for game text %s\n", id.c_str());
                    continue;
                }
                // These two ROM tables place each visible fragment after
                // zero-filled alignment bytes. The table pointers used by
                // the game skip that padding, so it must not participate in
                // the runtime source comparison. Keep the generated record
                // IDs stable and normalize only the bytes being matched.
                if (source.category == "item_description" ||
                    source.category == "item_notification") {
                    const auto visibleBegin = std::find_if(
                        source.bytes.begin(),
                        source.bytes.end(),
                        [](std::uint8_t value) { return value != 0; }
                    );
                    source.bytes.erase(source.bytes.begin(), visibleBegin);
                }
                if (source.category == "item_name") {
                    itemNameSourceCount++;
                }
                else if (source.category == "item_notification") {
                    itemNotificationSourceCount++;
                }
                gameSources.emplace(id, std::move(source));
            }

            std::size_t gameTranslatedCount = 0;
            for (const nlohmann::json& record : gameTranslationJson.at("records")) {
                const std::string id = record.at("id").get<std::string>();
                const std::string category = record.at("category").get<std::string>();
                const std::string translation = record.at("translation").get<std::string>();
                if (translation.empty()) {
                    continue;
                }

                const auto source = gameSources.find(id);
                if (source == gameSources.end() || source->second.bytes.empty()) {
                    std::fprintf(stderr, "Game text %s has no source entry\n", id.c_str());
                    continue;
                }
                if (source->second.category != category) {
                    std::fprintf(stderr, "Game text %s has a category mismatch\n", id.c_str());
                    continue;
                }

                std::u32string decodedTranslation;
                if (!decodeUtf8(translation, decodedTranslation)) {
                    std::fprintf(stderr, "Game text %s is not valid UTF-8\n", id.c_str());
                    continue;
                }

                TranslationEntry entry;
                entry.source = source->second.bytes;
                bool encoded = false;
                if (category == "system_message" || category == "menu") {
                    encoded = encodeMessage(
                        pack.font,
                        entry.source.front(),
                        decodedTranslation,
                        entry.replacement,
                        error
                    );
                    if (encoded) {
                        pack.entries.push_back(std::move(entry));
                    }
                }
                else if (category == "item_description") {
                    encoded = encodeFragment(
                        pack.font,
                        decodedTranslation,
                        kMessageEnd,
                        entry.replacement,
                        error
                    );
                    if (encoded) {
                        pack.itemDescriptions.push_back(std::move(entry));
                    }
                }
                else if (category == "item_name") {
                    encoded = encodeFragment(
                        pack.font,
                        decodedTranslation,
                        kNewLine,
                        entry.replacement,
                        error
                    );
                    if (encoded) {
                        pack.itemNames.push_back(std::move(entry));
                    }
                }
                else if (category == "item_notification") {
                    encoded = encodeFragment(
                        pack.font,
                        decodedTranslation,
                        kMessageEnd,
                        entry.replacement,
                        error
                    );
                    if (encoded) {
                        pack.itemNotifications.push_back(std::move(entry));
                    }
                }
                else {
                    error = "unknown game text category";
                }

                if (!encoded) {
                    std::fprintf(
                        stderr,
                        "Skipping game text %s: %s\n",
                        id.c_str(),
                        error.c_str()
                    );
                    continue;
                }
                gameTranslatedCount++;
            }
            pack.itemCompositionComplete = itemNameSourceCount > 0 &&
                itemNotificationSourceCount > 0 &&
                pack.itemNames.size() == itemNameSourceCount &&
                pack.itemNotifications.size() == itemNotificationSourceCount;

            pack.loaded = true;
            std::printf(
                "Text localization loaded: %zu dialogues and %zu other records from %s\n",
                translatedCount,
                gameTranslatedCount,
                pathToUtf8(translationPath).c_str()
            );
            std::fflush(stdout);
        }
        catch (const std::exception& exception) {
            std::fprintf(
                stderr,
                "Unable to load dialogue localization %s: %s\n",
                pathToUtf8(translationPath).c_str(),
                exception.what()
            );
            return false;
        }

        return true;
    }

    LanguagePack* languagePack(std::size_t language) {
        ensureLanguagesInitialized();
        if (language >= languagePacks.size() || languages[language].original) {
            return nullptr;
        }

        LanguagePack& pack = languagePacks[language];
        if (!pack.loadAttempted) {
            loadLanguagePack(language, pack);
        }
        return pack.loaded ? &pack : nullptr;
    }

    bool messageEquals(
        const std::uint8_t* rdram,
        std::uint32_t messageAddress,
        const std::vector<std::uint8_t>& expected
    ) {
        for (std::size_t index = 0; index < expected.size(); index++) {
            if (readByte(
                    rdram,
                    messageAddress + static_cast<std::uint32_t>(index)
                ) != expected[index]) {
                return false;
            }
        }
        return true;
    }

    const TranslationEntry* findTranslation(
        const std::uint8_t* rdram,
        std::uint32_t messageAddress,
        const std::vector<TranslationEntry>& entries
    ) {
        for (const TranslationEntry& entry : entries) {
            if (messageEquals(rdram, messageAddress, entry.source)) {
                return &entry;
            }
        }
        return nullptr;
    }

    bool ensureScratch(std::uint8_t* rdram) {
        if (state.messageAddress != 0) {
            return true;
        }

        std::uint8_t* scratch = static_cast<std::uint8_t*>(recomp::alloc(
            rdram,
            kMessageScratchSize + kAtlasSize
        ));
        if (scratch == nullptr) {
            if (!state.allocationFailureReported) {
                std::fprintf(stderr, "Unable to allocate dialogue localization memory\n");
                state.allocationFailureReported = true;
            }
            return false;
        }

        const auto offset = static_cast<std::uint32_t>(scratch - rdram);
        state.messageAddress = kN64Kseg0Base + offset;
        state.atlasAddress = state.messageAddress + kMessageScratchSize;
        return true;
    }

    SDL_Surface* loadAtlasBitmap(const std::filesystem::path& path) {
        const std::string utf8Path = pathToUtf8(path);
        SDL_Surface* source = SDL_LoadBMP(utf8Path.c_str());
        if (source == nullptr) {
            return nullptr;
        }

        SDL_Surface* converted = SDL_ConvertSurfaceFormat(
            source, SDL_PIXELFORMAT_RGBA32, 0
        );
        SDL_FreeSurface(source);
        return converted;
    }

    std::uint8_t cursorPaletteIndex(const std::uint8_t* pixel) {
        int bestDistance = 3 * 255 * 255 + 1;
        std::uint8_t bestIndex = 0;
        for (std::size_t index = 0; index < kAtlasPaletteColors.size(); index++) {
            const auto& color = kAtlasPaletteColors[index];
            const int redDifference = static_cast<int>(pixel[0]) - color[0];
            const int greenDifference = static_cast<int>(pixel[1]) - color[1];
            const int blueDifference = static_cast<int>(pixel[2]) - color[2];
            const int distance = redDifference * redDifference +
                greenDifference * greenDifference + blueDifference * blueDifference;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = static_cast<std::uint8_t>(index);
            }
        }
        return bestIndex;
    }

    bool applyAtlasBitmap(
        std::uint8_t* rdram,
        SDL_Surface* surface,
        const FontConfig& font
    ) {
        if (surface == nullptr ||
            surface->w != static_cast<int>(kAtlasWidth) ||
            surface->h != static_cast<int>(kAtlasHeight) ||
            surface->format->format != SDL_PIXELFORMAT_RGBA32) {
            return false;
        }

        if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
            return false;
        }

        std::array<bool, 256> atlasCodes = font.cursorCodes;
        for (const auto& [character, code] : font.glyphs) {
            static_cast<void>(character);
            atlasCodes[code] = true;
        }

        const auto* pixels = static_cast<const std::uint8_t*>(surface->pixels);
        for (std::size_t codeValue = 0; codeValue < atlasCodes.size(); codeValue++) {
            if (!atlasCodes[codeValue]) {
                continue;
            }
            const auto code = static_cast<std::uint8_t>(codeValue);

            const std::uint32_t tileAddress = state.atlasAddress +
                (static_cast<std::uint32_t>(code >> 4) * 0x900U) +
                (static_cast<std::uint32_t>(code & 0x0FU) * kGlyphSize);
            const int sourceTileX = (code & 0x0F) * static_cast<int>(kGlyphSize);
            const int sourceTileY = (code >> 4) * static_cast<int>(kGlyphSize);

            for (int y = 0; y < static_cast<int>(kGlyphSize); y++) {
                const auto* row = pixels +
                    (sourceTileY + y) * surface->pitch + sourceTileX * 4;
                for (int x = 0; x < static_cast<int>(kGlyphSize); x++) {
                    const auto* pixel = row + x * 4;
                    std::uint8_t paletteIndex = 0;
                    if (font.cursorCodes[codeValue]) {
                        paletteIndex = cursorPaletteIndex(pixel);
                    }
                    else {
                        const unsigned int brightness =
                            (static_cast<unsigned int>(pixel[0]) +
                             static_cast<unsigned int>(pixel[1]) +
                             static_cast<unsigned int>(pixel[2])) / 3U;
                        paletteIndex = (brightness >= 192U)
                            ? 1
                            : ((brightness >= 64U) ? 2 : 0);
                    }
                    writeByte(
                        rdram,
                        tileAddress + static_cast<std::uint32_t>(y) * kAtlasWidth +
                            static_cast<std::uint32_t>(x),
                        paletteIndex
                    );
                }
            }
        }

        if (SDL_MUSTLOCK(surface)) {
            SDL_UnlockSurface(surface);
        }
        return true;
    }

    bool ensureAtlas(
        std::uint8_t* rdram,
        std::uint32_t dialogueObjectAddress,
        const FontConfig& font
    ) {
        if (!ensureScratch(rdram)) {
            return false;
        }

        const std::uint32_t currentAtlasAddress = readWord(
            rdram, dialogueObjectAddress + kFontPointerOffset
        );
        if (!isRdramAddress(currentAtlasAddress) ||
            currentAtlasAddress == state.atlasAddress) {
            return false;
        }

        if (state.originalAtlasAddress != currentAtlasAddress) {
            state.originalAtlasAddress = currentAtlasAddress;
            state.atlasReady = false;
            state.loadedFontConfigPath.clear();
        }
        if (state.atlasReady && state.loadedFontConfigPath == font.configPath) {
            return true;
        }

        SDL_Surface* atlasBitmap = loadAtlasBitmap(font.atlasPath);
        if (atlasBitmap == nullptr) {
            std::fprintf(
                stderr,
                "Unable to load dialogue atlas %s\n",
                pathToUtf8(font.atlasPath).c_str()
            );
            return false;
        }

        for (std::uint32_t index = 0; index < kAtlasSize; index++) {
            writeByte(
                rdram,
                state.atlasAddress + index,
                readByte(rdram, state.originalAtlasAddress + index)
            );
        }

        const bool applied = applyAtlasBitmap(rdram, atlasBitmap, font);
        SDL_FreeSurface(atlasBitmap);
        if (!applied) {
            std::fprintf(
                stderr,
                "%s must be an uncompressed 192x192 BMP\n",
                pathToUtf8(font.atlasPath).c_str()
            );
            return false;
        }

        state.atlasReady = true;
        state.loadedFontConfigPath = font.configPath;
        std::printf(
            "Dialogue localization atlas loaded from %s\n",
            pathToUtf8(font.atlasPath).c_str()
        );
        std::fflush(stdout);
        return true;
    }

    bool copyMessageToScratch(
        std::uint8_t* rdram,
        const std::vector<std::uint8_t>& message,
        std::optional<std::uint8_t> prefix = std::nullopt
    ) {
        const std::size_t prefixSize = prefix.has_value() ? 1U : 0U;
        if (!ensureScratch(rdram) ||
            message.size() + prefixSize > kScratchSlotSize) {
            return false;
        }

        if (prefix.has_value()) {
            writeByte(rdram, state.messageAddress, *prefix);
        }
        for (std::size_t index = 0; index < message.size(); index++) {
            writeByte(
                rdram,
                state.messageAddress + static_cast<std::uint32_t>(index + prefixSize),
                message[index]
            );
        }
        return true;
    }

    void restoreOriginalAtlas(
        std::uint8_t* rdram,
        std::uint32_t dialogueObjectAddress
    ) {
        state.activeGlyphAdvance = 0;
        if (state.originalAtlasAddress == 0 || state.atlasAddress == 0) {
            return;
        }

        const std::uint32_t currentAtlasAddress = readWord(
            rdram, dialogueObjectAddress + kFontPointerOffset
        );
        if (currentAtlasAddress == state.atlasAddress) {
            writeWord(
                rdram,
                dialogueObjectAddress + kFontPointerOffset,
                state.originalAtlasAddress
            );
        }
    }
}

void doraemon::localization::initialize() {
    ensureLanguagesInitialized();
}

const std::vector<doraemon::localization::LanguageInfo>&
doraemon::localization::available_languages() {
    ensureLanguagesInitialized();
    return languages;
}

std::size_t doraemon::localization::find_language(std::string_view code) {
    ensureLanguagesInitialized();
    for (std::size_t index = 0; index < languages.size(); index++) {
        if (languages[index].code == code) {
            return index;
        }
    }
    return 0;
}

void doraemon::localization::set_language(std::size_t language) {
    ensureLanguagesInitialized();
    if (language >= languages.size()) {
        language = 0;
    }
    selectedLanguage.store(language, std::memory_order_release);
    composedMessageUsesLocalizedFont = false;
    localizedComposedMessages.clear();
    pendingComposedMessages.clear();
}

std::size_t doraemon::localization::language() {
    ensureLanguagesInitialized();
    return selectedLanguage.load(std::memory_order_acquire);
}

const doraemon::localization::LanguageInfo&
doraemon::localization::current_language() {
    ensureLanguagesInitialized();
    std::size_t index = selectedLanguage.load(std::memory_order_acquire);
    if (index >= languages.size()) {
        index = 0;
    }
    return languages[index];
}

std::filesystem::path doraemon::localization::texture_directory(
    const LanguageInfo& language
) {
    ensureLanguagesInitialized();
    if (language.original || localizationRoot.empty() ||
        !validLanguageCode(language.code)) {
        return {};
    }
    return localizationRoot / language.code / "textures";
}

extern "C" int doraemon_dialogue_glyph_advance(
    std::uint8_t* rdram,
    std::uint32_t dialogueObjectAddress
) {
    if (rdram == nullptr || !isRdramAddress(dialogueObjectAddress) ||
        state.atlasAddress == 0) {
        return 0;
    }

    const std::uint32_t currentAtlasAddress = readWord(
        rdram, dialogueObjectAddress + kFontPointerOffset
    );
    return (currentAtlasAddress == state.atlasAddress)
        ? state.activeGlyphAdvance
        : 0;
}

extern "C" void doraemon_localize_dialogue_message(
    std::uint8_t* rdram,
    std::uint32_t dialogueObjectAddress
) {
    if (rdram == nullptr || !isRdramAddress(dialogueObjectAddress)) {
        return;
    }

    restoreOriginalAtlas(rdram, dialogueObjectAddress);
    const std::size_t language =
        selectedLanguage.load(std::memory_order_acquire);
    LanguagePack* pack = languagePack(language);
    if (pack == nullptr) {
        return;
    }

    const std::uint32_t messageAddress = readWord(
        rdram, dialogueObjectAddress + kMessagePointerOffset
    );
    if (!isRdramAddress(messageAddress)) {
        return;
    }

    const TranslationEntry* translation = findTranslation(
        rdram, messageAddress, pack->entries
    );
    const TranslationEntry* itemDescription = nullptr;
    if (translation == nullptr && messageAddress + 1U < kN64Kseg0End) {
        itemDescription = findTranslation(
            rdram, messageAddress + 1U, pack->itemDescriptions
        );
    }
    const bool composedTranslation =
        localizedComposedMessages.contains(messageAddress);
    if (translation == nullptr && itemDescription == nullptr &&
        !composedTranslation) {
        return;
    }
    if (!ensureAtlas(rdram, dialogueObjectAddress, pack->font)) {
        return;
    }

    // The game copies a composed-message pointer into the dialogue object
    // before this parser hook runs. Keep the temporary pointer table active
    // until every populated output slot has reached a dialogue object, then
    // restore the game's original buffers. Restoring it in the composer itself
    // is too early: the result screen reads the table only afterwards.
    if (composedTranslation) {
        pendingComposedMessages.erase(messageAddress);
        if (pendingComposedMessages.empty()) {
            restoreComposedMessagePointers(rdram);
        }
    }

    if (translation != nullptr) {
        if (!copyMessageToScratch(rdram, translation->replacement)) {
            return;
        }
        writeWord(
            rdram,
            dialogueObjectAddress + kMessagePointerOffset,
            state.messageAddress
        );
    }
    else if (itemDescription != nullptr) {
        if (!copyMessageToScratch(
                rdram,
                itemDescription->replacement,
                readByte(rdram, messageAddress)
            )) {
            return;
        }
        writeWord(
            rdram,
            dialogueObjectAddress + kMessagePointerOffset,
            state.messageAddress
        );
    }

    writeWord(
        rdram,
        dialogueObjectAddress + kFontPointerOffset,
        state.atlasAddress
    );
    state.activeGlyphAdvance = pack->font.glyphWidth + pack->font.glyphSpacing;
}

extern "C" void doraemon_begin_game_text_composition(std::uint8_t* rdram) {
    composedMessageUsesLocalizedFont = false;
    localizedComposedMessages.clear();
    pendingComposedMessages.clear();

    if (rdram == nullptr) {
        return;
    }
    // A previous composition should always finish, but restore defensively if
    // the game starts another one through an unusual result-screen branch.
    restoreComposedMessagePointers(rdram);
    const std::size_t language =
        selectedLanguage.load(std::memory_order_acquire);
    LanguagePack* pack = languagePack(language);
    if (pack == nullptr || !pack->itemCompositionComplete ||
        !ensureScratch(rdram)) {
        return;
    }

    // The original game uses two small fixed buffers here. Localized item
    // names can contain many more one-byte glyphs even though they are much
    // narrower on screen, so compose into two independent large slots in the
    // persistent localization allocation instead.
    for (std::uint32_t index = 0; index < kComposedMessageOffsets.size(); index++) {
        originalComposedMessagePointers[index] = readWord(
            rdram,
            kComposedMessagePointerTable + index * 4U
        );
    }
    composedMessagePointersRedirected = true;
    for (std::uint32_t index = 0; index < kComposedMessageOffsets.size(); index++) {
        writeWord(
            rdram,
            kComposedMessagePointerTable + index * 4U,
            state.messageAddress + kComposedMessageOffsets[index]
        );
    }
    // Mark both output slots empty. The two-item notification composer fills
    // both, while the result-table composer fills only the first one.
    writeByte(
        rdram,
        state.messageAddress + kFirstComposedMessageOffset,
        kMessageEnd
    );
    writeByte(
        rdram,
        state.messageAddress + kSecondComposedMessageOffset,
        kMessageEnd
    );
}

extern "C" std::uint32_t doraemon_localize_game_text_fragment(
    std::uint8_t* rdram,
    std::uint32_t sourceAddress,
    int category,
    int prefixMode
) {
    if (rdram == nullptr || !isRdramAddress(sourceAddress)) {
        return sourceAddress;
    }

    const std::size_t language =
        selectedLanguage.load(std::memory_order_acquire);
    LanguagePack* pack = languagePack(language);
    if (pack == nullptr || !pack->itemCompositionComplete) {
        return sourceAddress;
    }

    const std::vector<TranslationEntry>* entries = nullptr;
    if (category == 1) {
        entries = &pack->itemNames;
    }
    else if (category == 2) {
        entries = &pack->itemNotifications;
    }
    if (entries == nullptr) {
        return sourceAddress;
    }

    // A fragment that starts a composed dialogue retains the one-byte message
    // prefix before its visible F8/color sequence. Prefix mode 1 receives the
    // original pointer and therefore skips that byte for matching. Prefix mode
    // 2 receives a pointer that the game has already advanced past the prefix;
    // the best-time composer still needs that preceding byte restored so its
    // leading F8 is parsed as a color control instead of its 32 argument being
    // drawn as the Cyrillic glyph at code 0x32.
    const bool sourceIncludesPrefix = prefixMode == 1;
    const bool restorePreviousPrefix = prefixMode == 2;
    if (prefixMode < 0 || prefixMode > 2) {
        return sourceAddress;
    }
    std::uint32_t matchAddress = sourceIncludesPrefix
        ? sourceAddress + 1U
        : sourceAddress;
    const TranslationEntry* translation = nullptr;
    // Some notification pointers retain up to eleven original spacing bytes
    // before the visible suffix. The extracted sources intentionally omit
    // that layout padding, so try the current address and then advance only
    // across zero/space bytes until the normalized fragment starts.
    for (std::size_t skipped = 0; skipped <= 16U; skipped++) {
        if (!isRdramAddress(matchAddress)) {
            break;
        }
        translation = findTranslation(rdram, matchAddress, *entries);
        if (translation != nullptr || readByte(rdram, matchAddress) != 0) {
            break;
        }
        matchAddress++;
    }
    if (translation == nullptr) {
        return sourceAddress;
    }

    std::optional<std::uint8_t> serviceByte;
    if (sourceIncludesPrefix) {
        serviceByte = readByte(rdram, sourceAddress);
    }
    else if (restorePreviousPrefix &&
             sourceAddress > kN64Kseg0Base &&
             isRdramAddress(sourceAddress - 1U)) {
        serviceByte = readByte(rdram, sourceAddress - 1U);
    }
    if (!copyMessageToScratch(rdram, translation->replacement, serviceByte)) {
        return sourceAddress;
    }

    composedMessageUsesLocalizedFont = true;
    return state.messageAddress;
}

extern "C" std::uint32_t doraemon_localize_time_label(
    std::uint8_t* rdram,
    std::uint32_t sourceAddress,
    int labelIndex
) {
    if (rdram == nullptr || labelIndex < 0 || labelIndex > 1) {
        return sourceAddress;
    }

    const std::size_t language =
        selectedLanguage.load(std::memory_order_acquire);
    LanguagePack* pack = languagePack(language);
    if (pack == nullptr || !pack->itemCompositionComplete ||
        static_cast<std::size_t>(labelIndex) >= pack->itemNames.size() ||
        !ensureScratch(rdram)) {
        return sourceAddress;
    }

    const TranslationEntry& translation =
        pack->itemNames[static_cast<std::size_t>(labelIndex)];

    // Both original result labels are preceded by a zero layout/service byte.
    // It is deliberately retained: the dialogue parser skips the first byte,
    // so F8 remains a color command instead of being swallowed as the prefix.
    if (!copyMessageToScratch(
            rdram,
            translation.replacement,
            std::optional<std::uint8_t>{0U}
        )) {
        return sourceAddress;
    }

    composedMessageUsesLocalizedFont = true;
    return state.messageAddress;
}

extern "C" std::uint32_t doraemon_localize_game_text_digit(
    std::uint32_t originalCode
) {
    if (!composedMessageUsesLocalizedFont || originalCode < 1U ||
        originalCode > 10U) {
        return originalCode;
    }

    const std::size_t language =
        selectedLanguage.load(std::memory_order_acquire);
    LanguagePack* pack = languagePack(language);
    if (pack == nullptr) {
        return originalCode;
    }

    const char32_t character = U'0' + static_cast<char32_t>(originalCode - 1U);
    const auto glyph = pack->font.glyphs.find(character);
    return glyph != pack->font.glyphs.end()
        ? glyph->second
        : originalCode;
}

extern "C" void doraemon_finish_game_text_composition(std::uint8_t* rdram) {
    localizedComposedMessages.clear();
    pendingComposedMessages.clear();
    if (rdram != nullptr) {
        for (std::uint32_t offset : {0U, 4U}) {
            const std::uint32_t address = readWord(
                rdram, kComposedMessagePointerTable + offset
            );
            if (isRdramAddress(address) &&
                readByte(rdram, address) != kMessageEnd) {
                // Both users of this composer create system messages: item
                // notifications and the result-time table. Neither has a
                // speaker portrait. The dialogue parser treats the first byte
                // as a portrait ID, and a few original-language branches can
                // leave a stale nonzero value in these reused output buffers.
                // Normalize the service byte for every language instead of
                // special-casing individual Japanese strings.
                writeByte(rdram, address, kNoPortrait);
                if (composedMessageUsesLocalizedFont) {
                    localizedComposedMessages.insert(address);
                    pendingComposedMessages.insert(address);
                }
            }
        }
    }
    // Consumers copy these pointers into their dialogue objects after the
    // composer returns. doraemon_localize_dialogue_message restores the table
    // once every populated slot has been observed. If no localized output was
    // produced, nothing needs the redirected table.
    if (pendingComposedMessages.empty()) {
        restoreComposedMessagePointers(rdram);
    }
    composedMessageUsesLocalizedFont = false;
}

extern "C" void doraemon_prepare_localization_for_game_reset(
    std::uint8_t* rdram
) {
    // The translated font atlas and message scratch area are allocated from
    // the emulated heap. A cold reset recreates that heap, so no host-side
    // address from the previous session may survive into the next one.
    restoreComposedMessagePointers(rdram);
    state = LocalizationState{};
    composedMessageUsesLocalizedFont = false;
    localizedComposedMessages.clear();
    pendingComposedMessages.clear();
    originalComposedMessagePointers = {};
    composedMessagePointersRedirected = false;
}
