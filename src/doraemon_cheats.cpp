#include "doraemon_cheats.h"
#include "librecomp/addresses.hpp"
#include <atomic>

namespace {
    constexpr uint32_t GameState=0x800F38A0, Actors=0x800FB820;
    constexpr unsigned Health=1, Lives=2, Torpedo=4;
    std::atomic<unsigned> options{0};
    std::atomic_bool raceActive{false}, firstRaceProfile{false};
    // Accessed only on the guest thread, or at the existing reset barrier.
    uint32_t torpedoActor=0;
    uint32_t player_actor(uint8_t* rdram) {
        const unsigned slot=MEM_BU(0x41,gpr(S32(GameState)));
        if(slot>=4) return 0;
        const int index=MEM_H(slot*0x68,gpr(S32(0x801591C0)));
        if(index<0 || index>=256) return 0;
        const uint32_t actor=Actors+unsigned(index)*0x100;
        if(MEM_H(0,gpr(S32(actor)))!=1) return 0;
        return actor;
    }
}

void doraemon_cheats_configure(int health, int lives, int torpedo) {
    options.store((health?Health:0) | (lives?Lives:0) | (torpedo?Torpedo:0));
}
int doraemon_cheats_any_enabled() {
    return options.load()!=0 || (raceActive.load() && firstRaceProfile.load());
}
int doraemon_cheats_race_active() { return raceActive.load(); }
void doraemon_cheats_reset() {
    torpedoActor=0; raceActive.store(false); firstRaceProfile.store(false);
}
void doraemon_cheats_tick(uint8_t* rdram) {
    const unsigned flags=options.load();
    if(!(flags&(Health|Lives))) return;
    const uint32_t actor=player_actor(rdram);
    if(!actor) return;
    if(flags&Health) {
        MEM_W(0x74,gpr(S32(actor)))=250;
        MEM_H(0x20,gpr(S32(GameState)))=250; // HP carried through scene transitions.
    }
    if(flags&Lives) MEM_H(0x24,gpr(S32(GameState)))=9;
}
uint64_t doraemon_cheats_health_value(uint8_t* rdram, uint32_t address, uint64_t value) {
    if(options.load()&Health) {
        const uint32_t actor=player_actor(rdram);
        if(actor && address==actor+0x74) return 250;
    }
    return value;
}
uint64_t doraemon_cheats_lives_value(uint32_t address, uint64_t value) {
    return (options.load()&Lives) && address==GameState+0x24 ? 9 : value;
}
void doraemon_cheats_torpedo_update(uint8_t* rdram, int index) {
    if(index<0 || index>=256) return;
    const uint32_t actor=Actors+unsigned(index)*0x100;
    // Called only by the torpedo callback (func_80031254), independent of model ID.
    if(MEM_H(0,gpr(S32(actor)))!=1) return;
    const int state=MEM_W(4,gpr(S32(actor)));
    // Freeze the route choice only for the moving race, not the introduction.
    // The three racing paths have different waypoint counts; switching in flight
    // could reinterpret the current waypoint and teleport the opponent.
    if(state<25) {
        torpedoActor=actor;
        firstRaceProfile.store((options.load()&Torpedo)!=0);
        raceActive.store(false);
    }
    else if(state==25 && actor==torpedoActor) raceActive.store(true);
    else if(state==30 && actor==torpedoActor) {
        raceActive.store(false); firstRaceProfile.store(false);
    }
}
uint32_t doraemon_cheats_torpedo_route(uint32_t actor, uint32_t route) {
    // Only replace the two racing profiles, never progression, rewards or the
    // intro/exit routes. The actual route field and its increments stay original.
    return actor==torpedoActor && firstRaceProfile.load() && (route==4 || route==7) ? 1 : route;
}
