#include "doraemon_cheats.h"
#include "librecomp/addresses.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void check(bool ok, const char* message) {
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main() {
    std::vector<uint8_t> memory(0x400000); auto* rdram=memory.data();
    constexpr uint32_t player=0x800FB820+7*0x100, enemy=player+0x100;
    constexpr uint32_t hp=player+0x74, lives=0x800F38C4;
    MEM_B(0,gpr(S32(0x800F38E1)))=2;
    MEM_H(2*0x68,gpr(S32(0x801591C0)))=7;
    MEM_H(0,gpr(S32(player)))=1;
    MEM_H(0x48,gpr(S32(player)))=3;
    MEM_W(0x74,gpr(S32(player)))=10;
    MEM_H(0,gpr(S32(lives)))=2;
    const auto original=memory;
    doraemon_cheats_tick(rdram);
    check(memory==original && !doraemon_cheats_any_enabled(),"all cheats default off");
    constexpr uint64_t registerValue=0x12345678000000F0ULL;
    check(doraemon_cheats_health_value(rdram,hp,registerValue)==registerValue,"disabled HP hook preserves full register");
    doraemon_cheats_configure(1,0,0); doraemon_cheats_tick(rdram);
    check(MEM_W(0,gpr(S32(hp)))==250 && MEM_HU(0,gpr(S32(lives)))==2,"health toggle isolated from lives");
    check(doraemon_cheats_health_value(rdram,hp,0)==250,"lethal damage prevented before death check");
    check(doraemon_cheats_health_value(rdram,enemy+0x74,registerValue)==registerValue,"enemy health unchanged");
    check(doraemon_cheats_health_value(rdram,hp+4,0)==0,"adjacent actor field unchanged");
    MEM_H(0,gpr(S32(player)))=0;
    check(doraemon_cheats_health_value(rdram,hp,0)==0,"freed actor ignored");
    MEM_H(0,gpr(S32(player)))=1;
    doraemon_cheats_configure(0,1,0); doraemon_cheats_tick(rdram);
    check(MEM_HU(0,gpr(S32(lives)))==9,"lives become nine");
    check(doraemon_cheats_lives_value(lives,8)==9 && doraemon_cheats_lives_value(lives,0)==9,"life loss and game-over count protected");
    check(doraemon_cheats_lives_value(lives+2,0)==0,"other counters unchanged");
    check(doraemon_cheats_health_value(rdram,hp,0)==0,"HP can be disabled independently");
    MEM_B(0,gpr(S32(0x800F38E1)))=4;
    const auto invalid=memory; doraemon_cheats_tick(rdram); check(memory==invalid,"invalid controller ignored");
    MEM_B(0,gpr(S32(0x800F38E1)))=2;
    constexpr uint32_t torpedo=0x800FB820+12*0x100;
    MEM_H(0,gpr(S32(torpedo)))=1; MEM_H(0x48,gpr(S32(torpedo)))=0x6D;
    for(unsigned route : {1U,4U,7U}) {
        doraemon_cheats_reset(); doraemon_cheats_configure(0,0,1);
        MEM_W(4,gpr(S32(torpedo)))=20; doraemon_cheats_torpedo_update(rdram,12);
        check(doraemon_cheats_torpedo_route(torpedo,route)==1,"all three races use first movement profile");
        check(doraemon_cheats_torpedo_route(enemy,route)==route,"other actors unaffected");
        check(doraemon_cheats_torpedo_route(torpedo,route-1)==route-1,"intro profile preserved");
        MEM_W(4,gpr(S32(torpedo)))=25; doraemon_cheats_torpedo_update(rdram,12);
        check(doraemon_cheats_race_active(),"active race marked");
        doraemon_cheats_configure(0,0,0);
        check(doraemon_cheats_torpedo_route(torpedo,route)==1 && doraemon_cheats_any_enabled(),"running profile stable and indicator truthful until finish");
        MEM_W(4,gpr(S32(torpedo)))=30; doraemon_cheats_torpedo_update(rdram,12);
        check(!doraemon_cheats_any_enabled() && !doraemon_cheats_race_active(),"finish releases race state");
    }
    doraemon_cheats_reset();
    MEM_W(4,gpr(S32(torpedo)))=20; doraemon_cheats_torpedo_update(rdram,12);
    check(doraemon_cheats_torpedo_route(torpedo,7)==7,"disabled race keeps original difficulty");
    std::puts("Cheats: independent toggles, player-only HP, nine lives, race profiles and reset passed.");
}
