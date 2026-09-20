#include "doraemon_lighting.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
    void require(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(EXIT_FAILURE);
        }
    }
}

int main() {
    using namespace doraemon::lighting;
    std::vector<std::uint8_t> ram(0x800000);
    constexpr std::array<std::uint32_t, 3> sources = {
        0x142BF0, // Object Light.
        0x143BF0, // World geometry Light.
        0x14DFB0  // Ambient Light.
    };
    constexpr std::uint32_t first = 0xCA360;
    constexpr std::uint32_t second = 0xD24B0;

    auto word = [&](std::uint32_t address, std::uint32_t value) {
        std::memcpy(ram.data() + address, &value, 4);
    };
    auto prepare = [&](std::uint32_t buffer, std::uint8_t value) {
        const auto root = first + buffer * 0x8150;
        for (std::size_t i = 0; i < sources.size(); i++) {
            word(root + static_cast<std::uint32_t>(i * 8), 0x03860010 + static_cast<std::uint32_t>(i * 0x20000));
            word(root + static_cast<std::uint32_t>(i * 8 + 4), 0x80000000 | sources[i]);
            for (unsigned byte = 0; byte < 16; byte++) {
                ram[sources[i] + byte] = static_cast<std::uint8_t>(value + i + byte);
            }
        }
        word(root + 24, 0xBC000002);
        word(root + 28, 0x80000060);
        word(0xCA240 + buffer * 0x8150 + 0x814C, 0x80000000 | (root + 32));

        const auto original = ram;
        doraemon_snapshot_frame_lights(ram.data(), buffer);
        require(ram == original, "capturing light must not modify guest RDRAM");
    };
    auto check = [&](std::uint8_t value) {
        for (std::size_t i = 0; i < sources.size(); i++) {
            const auto* data = static_cast<const std::uint8_t*>(light_data(sources[i]));
            require(data != nullptr, "the task must have a light snapshot");
            for (unsigned byte = 0; byte < 16; byte++) {
                require(data[byte] == static_cast<std::uint8_t>(value + i + byte),
                    "a newer frame must not replace this task's RGB/direction");
            }
        }
    };

    reset();
    prepare(0, 0x12);
    prepare(1, 0x32); // Reuse guest Light memory before parsing frame 0.
    const auto original = ram;
    begin_task(first, 32);
    check(0x12);
    require(light_data(0xB50D0) == nullptr, "ROM lights must use the normal RDRAM path");
    require(light_data(0x142BB7) == nullptr, "reject addresses before the Light pool");
    require(light_data(0x14F5E1) == nullptr, "reject Light records crossing the pool end");
    end_task();
    require(ram == original, "rendering must not modify guest RDRAM");
    begin_task(second, 32);
    check(0x32);
    end_task();

    // A task owns its snapshot even if the corresponding slot is republished.
    begin_task(first, 32);
    const void* retained = light_data(sources[0]);
    prepare(0, 0x52);
    require(light_data(sources[0]) == retained, "retain storage throughout parsing");
    check(0x12);
    end_task();
    begin_task(first, 32);
    check(0x52);
    end_task();

    begin_task(first, 16);
    require(light_data(sources[0]) == nullptr, "reject an incomplete task range");
    end_task();
    begin_task(first + 8, 32);
    require(light_data(sources[0]) == nullptr, "reject a different display list root");
    end_task();

    reset();
    begin_task(first, 32);
    require(light_data(sources[0]) == nullptr, "reset must discard the previous game");
    end_task();
    prepare(0, 0x72);
    begin_task(first, 32);
    check(0x72);
    end_task();
    reset();

    std::puts("Lighting snapshot regression checks passed.");
}
