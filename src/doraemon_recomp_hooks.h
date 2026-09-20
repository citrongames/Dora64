#pragma once

#include "recomp.h"
#include "doraemon_model_pool.h"
#include "doraemon_draw_distance.h"
#include "doraemon_cheats.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void doraemon_collectible_autosave_poll(uint8_t* rdram, recomp_context* ctx);
int doraemon_game_reset_poll(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif
