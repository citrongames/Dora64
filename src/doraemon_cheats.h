#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void doraemon_cheats_configure(int health, int lives, int torpedo);
int doraemon_cheats_any_enabled(void);
int doraemon_cheats_race_active(void);
void doraemon_cheats_tick(uint8_t* rdram);
void doraemon_cheats_reset(void);
uint64_t doraemon_cheats_health_value(uint8_t* rdram, uint32_t address, uint64_t value);
uint64_t doraemon_cheats_lives_value(uint32_t address, uint64_t value);
void doraemon_cheats_torpedo_update(uint8_t* rdram, int index);
uint32_t doraemon_cheats_torpedo_route(uint32_t actor, uint32_t route);
#ifdef __cplusplus
}
#endif
