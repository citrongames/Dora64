#pragma once

// Temporary test4 diagnostic. Disabled unless debug.dora64.fast_cpu=1.
// Remove after the Huawei scheduling comparison; this is not a release policy.
#if defined(__ANDROID__)
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <iterator>
#include <map>
#include <sched.h>
#include <sys/system_properties.h>
#include <unistd.h>

namespace doraemon::cpu_probe {
inline std::atomic<pid_t> presentThread{0}, displayListThread{0};
inline void register_present() { presentThread.store(gettid(), std::memory_order_relaxed); }
inline void register_display_list() { displayListThread.store(gettid(), std::memory_order_relaxed); }

// Only called by the SDL event thread. The target threads merely publish IDs.
inline void update() {
    using Clock = std::chrono::steady_clock;
    static auto next = Clock::time_point{};
    const auto now = Clock::now();
    if (now < next) return;
    next = now + std::chrono::seconds(1);
    char value[PROP_VALUE_MAX]{};
    __system_property_get("debug.dora64.fast_cpu", value);
    const bool enabled = std::strcmp(value, "1") == 0;
    static std::map<pid_t, cpu_set_t> originals;
    if (!enabled) {
        for (auto it = originals.begin(); it != originals.end();) {
            if (sched_setaffinity(it->first, sizeof(cpu_set_t), &it->second) == 0 || errno == ESRCH) {
                std::fprintf(stderr, "Dora64 CPU probe: restored tid=%d\n", int(it->first));
                it = originals.erase(it);
            }
            else ++it;
        }
        return;
    }

    cpu_set_t available;
    if (sched_getaffinity(0, sizeof(available), &available) != 0) return;
    int capacities[CPU_SETSIZE]{};
    int minimum = 0, maximum = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &available)) continue;
        char path[128];
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpu_capacity", cpu);
        FILE* f = std::fopen(path, "r");
        if (!f) return; // No reliable topology: retain the system's scheduling.
        const int fields = std::fscanf(f, "%d", &capacities[cpu]);
        std::fclose(f);
        if (fields != 1 || capacities[cpu] <= 0) return;
        minimum = minimum == 0 ? capacities[cpu] : std::min(minimum, capacities[cpu]);
        maximum = std::max(maximum, capacities[cpu]);
    }
    if (maximum <= minimum) return;

    // Find only the workload thread. Shader, audio and game threads keep their masks.
    pid_t workload = 0;
    if (DIR* tasks = opendir("/proc/self/task")) {
        while (auto* entry = readdir(tasks)) {
            const auto tid = static_cast<pid_t>(std::atoi(entry->d_name));
            if (tid <= 0) continue;
            char path[128], name[64]{};
            std::snprintf(path, sizeof(path), "/proc/self/task/%d/comm", int(tid));
            if (FILE* f = std::fopen(path, "r")) {
                const bool read = std::fgets(name, sizeof(name), f) != nullptr;
                std::fclose(f);
                if (read && std::strcmp(name, "RT64 Workload\n") == 0) workload = tid;
            }
        }
        closedir(tasks);
    }
    const pid_t targets[] = {presentThread.load(std::memory_order_relaxed),
        displayListThread.load(std::memory_order_relaxed), workload};
    for (auto it = originals.begin(); it != originals.end();) {
        const bool active = std::find(std::begin(targets), std::end(targets), it->first) != std::end(targets);
        if (active) { ++it; continue; }
        if (sched_setaffinity(it->first, sizeof(cpu_set_t), &it->second) == 0 || errno == ESRCH)
            it = originals.erase(it);
        else ++it;
    }
    for (const auto tid : targets) {
        if (tid <= 0 || originals.count(tid)) continue;
        cpu_set_t original, selected;
        if (sched_getaffinity(tid, sizeof(original), &original) != 0) continue;
        CPU_ZERO(&selected);
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &original) && capacities[cpu] > minimum) CPU_SET(cpu, &selected);
        if (CPU_COUNT(&selected) == 0) continue;
        if (sched_setaffinity(tid, sizeof(selected), &selected) == 0) {
            originals.emplace(tid, original);
            std::fprintf(stderr, "Dora64 CPU probe: tid=%d selected %d performance CPUs, capacity=%d..%d\n",
                int(tid), CPU_COUNT(&selected), minimum, maximum);
        }
        else std::fprintf(stderr, "Dora64 CPU probe: tid=%d affinity failed errno=%d\n", int(tid), errno);
    }
}
}
#else
namespace doraemon::cpu_probe {
inline void register_present() {}
inline void register_display_list() {}
inline void update() {}
}
#endif
